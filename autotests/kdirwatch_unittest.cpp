/* This file is part of the KDE libraries

    Copyright (c) 2009 David Faure <faure@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include <kdirwatch.h>

#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <qtemporarydir.h>
#include <QtTest>
#include <sys/stat.h>
#ifdef Q_OS_UNIX
#include <unistd.h> // ::link()
#endif

// Debugging notes: to see which inotify signals are emitted, either set s_verboseDebug=true
// at the top of kdirwatch.cpp, or use the command-line tool "inotifywait -m /path"

// Note that kdirlistertest and kdirmodeltest also exercise KDirWatch quite a lot.

static const char *methodToString(KDirWatch::Method method)
{
    switch (method) {
    case KDirWatch::FAM:
        return "Fam";
    case KDirWatch::INotify:
        return "INotify";
    case KDirWatch::Stat:
        return "Stat";
    case KDirWatch::QFSWatch:
        return "QFSWatch";
    }
    return "ERROR!";
}

class StaticObject
{
public:
    KDirWatch m_dirWatch;
};
Q_GLOBAL_STATIC(StaticObject, s_staticObject)

class StaticObjectUsingSelf // like KSambaShare does, bug 353080
{
public:
    StaticObjectUsingSelf() {
        KDirWatch::self();
    }
    ~StaticObjectUsingSelf() {
        if (KDirWatch::exists() && KDirWatch::self()->contains(QDir::homePath())) {
            KDirWatch::self()->removeDir(QDir::homePath());
        }
    }
};
Q_GLOBAL_STATIC(StaticObjectUsingSelf, s_staticObjectUsingSelf)

class KDirWatch_UnitTest : public QObject
{
    Q_OBJECT
public:
    KDirWatch_UnitTest()
    {
        s_staticObjectUsingSelf();

        m_path = m_tempDir.path() + QLatin1Char('/');
        // Speed up the test by making the kdirwatch timer (to compress changes) faster
        qputenv("KDIRWATCH_POLLINTERVAL", "50");
        qputenv("KDIRWATCH_METHOD", KDIRWATCH_TEST_METHOD);
        KDirWatch *dirW = &s_staticObject()->m_dirWatch;
        m_slow = (dirW->internalMethod() == KDirWatch::FAM || dirW->internalMethod() == KDirWatch::Stat);
        qDebug() << "Using method" << methodToString(dirW->internalMethod());
    }

private Q_SLOTS: // test methods
    void initTestCase()
    {
        // By creating the files upfront, we save waiting a full second for an mtime change
        createFile(m_path + QLatin1String("ExistingFile"));
        createFile(m_path + QLatin1String("TestFile"));
        createFile(m_path + QLatin1String("nested_0"));
        createFile(m_path + QLatin1String("nested_1"));

        s_staticObject()->m_dirWatch.addFile(m_path + QLatin1String("ExistingFile"));
    }
    void touchOneFile();
    void touch1000Files();
    void watchAndModifyOneFile();
    void removeAndReAdd();
    void watchNonExistent();
    void watchNonExistentWithSingleton();
    void testDelete();
    void testDeleteAndRecreateFile();
    void testDeleteAndRecreateDir();
    void testMoveTo();
    void nestedEventLoop();
    void testHardlinkChange();
    void stopAndRestart();
    void shouldIgnoreQrcPaths();

protected Q_SLOTS: // internal slots
    void nestedEventLoopSlot();

private:
    void waitUntilMTimeChange(const QString &path);
    void waitUntilNewSecond();
    void waitUntilAfter(const QDateTime &ctime);
    QList<QVariantList> waitForDirtySignal(KDirWatch &watch, int expected);
    QList<QVariantList> waitForDeletedSignal(KDirWatch &watch, int expected);
    bool waitForOneSignal(KDirWatch &watch, const char *sig, const QString &path);
    bool waitForRecreationSignal(KDirWatch &watch, const QString &path);
    bool verifySignalPath(QSignalSpy &spy, const char *sig, const QString &expectedPath);
    void createFile(const QString &path);
    QString createFile(int num);
    void removeFile(int num);
    void appendToFile(const QString &path);
    void appendToFile(int num);

    QTemporaryDir m_tempDir;
    QString m_path;
    bool m_slow;
};

QTEST_MAIN(KDirWatch_UnitTest)

// Just to make the inotify packets bigger
static const char s_filePrefix[] = "This_is_a_test_file_";

static const int s_maxTries = 50;

// helper method: create a file
void KDirWatch_UnitTest::createFile(const QString &path)
{
    QFile file(path);
    bool ok = file.open(QIODevice::WriteOnly);
    Q_UNUSED(ok) // silence warnings
    Q_ASSERT(ok);
    file.write(QByteArray("foo"));
    file.close();
    //qDebug() << path;
}

// helper method: create a file (identified by number)
QString KDirWatch_UnitTest::createFile(int num)
{
    const QString fileName = QLatin1String(s_filePrefix) + QString::number(num);
    createFile(m_path + fileName);
    return m_path + fileName;
}

// helper method: delete a file (identified by number)
void KDirWatch_UnitTest::removeFile(int num)
{
    const QString fileName = QLatin1String(s_filePrefix) + QString::number(num);
    QFile::remove(m_path + fileName);
}

void KDirWatch_UnitTest::waitUntilMTimeChange(const QString &path)
{
    // Wait until the current second is more than the file's mtime
    // otherwise this change will go unnoticed

    QFileInfo fi(path);
    QVERIFY(fi.exists());
    const QDateTime ctime = qMax(fi.lastModified(), fi.created());
    waitUntilAfter(ctime);

}

void KDirWatch_UnitTest::waitUntilNewSecond()
{
    QDateTime now = QDateTime::currentDateTime();
    waitUntilAfter(now);
}

void KDirWatch_UnitTest::waitUntilAfter(const QDateTime &ctime)
{
    int totalWait = 0;
    QDateTime now;
    Q_FOREVER {
        now = QDateTime::currentDateTime();
        if (now.toTime_t() == ctime.toTime_t())   // truncate milliseconds
        {
            totalWait += 50;
            QTest::qWait(50);
        } else {
            QVERIFY(now > ctime); // can't go back in time ;)
            QTest::qWait(50); // be safe
            break;
        }
    }
    //if (totalWait > 0)
    qDebug() << "Waited" << totalWait << "ms so that now" << now.toString(Qt::ISODate) << "is >" << ctime.toString(Qt::ISODate);
}

// helper method: modifies a file
void KDirWatch_UnitTest::appendToFile(const QString &path)
{
    QVERIFY(QFile::exists(path));
    waitUntilMTimeChange(path);
    //const QString directory = QDir::cleanPath(path+"/..");
    //waitUntilMTimeChange(directory);

    QFile file(path);
    QVERIFY(file.open(QIODevice::Append | QIODevice::WriteOnly));
    file.write(QByteArray("foobar"));
    file.close();
#if 0
        QFileInfo fi(path);
        QVERIFY(fi.exists());
        qDebug() << "After append: file ctime=" << fi.lastModified().toString(Qt::ISODate);
        QVERIFY(fi.exists());
        qDebug() << "After append: directory mtime=" << fi.created().toString(Qt::ISODate);
#endif
}

// helper method: modifies a file (identified by number)
void KDirWatch_UnitTest::appendToFile(int num)
{
    const QString fileName = QLatin1String(s_filePrefix) + QString::number(num);
    appendToFile(m_path + fileName);
}

static QString removeTrailingSlash(const QString &path)
{
    if (path.endsWith(QLatin1Char('/'))) {
        return path.left(path.length() - 1);
    } else {
        return path;
    }
}

// helper method
QList<QVariantList> KDirWatch_UnitTest::waitForDirtySignal(KDirWatch &watch, int expected)
{
    QSignalSpy spyDirty(&watch, SIGNAL(dirty(QString)));
    int numTries = 0;
    // Give time for KDirWatch to notify us
    while (spyDirty.count() < expected) {
        if (++numTries > s_maxTries) {
            qWarning() << "Timeout waiting for KDirWatch. Got" << spyDirty.count() << "dirty() signals, expected" << expected;
            return spyDirty;
        }
        QTest::qWait(50);
    }
    return spyDirty;
}

bool KDirWatch_UnitTest::waitForOneSignal(KDirWatch &watch, const char *sig, const QString &path)
{
    const QString expectedPath = removeTrailingSlash(path);
    while (true) {
        QSignalSpy spyDirty(&watch, sig);
        int numTries = 0;
        // Give time for KDirWatch to notify us
        while (spyDirty.isEmpty()) {
            if (++numTries > s_maxTries) {
                qWarning() << "Timeout waiting for KDirWatch signal" << QByteArray(sig).mid(1) << "(" << path << ")";
                return false;
            }
            QTest::qWait(50);
        }
        return verifySignalPath(spyDirty, sig, expectedPath);
    }
}

bool KDirWatch_UnitTest::verifySignalPath(QSignalSpy &spy, const char *sig, const QString &expectedPath)
{
  for (int i = 0; i < spy.count(); ++i) {
    const QString got = spy[i][0].toString();
    if (got == expectedPath) {
      return true;
    }
    if (got.startsWith(expectedPath + QLatin1Char('/'))) {
      qDebug() << "Ignoring (inotify) notification of" << (sig + 1) << '(' << got << ')';
      continue;
    }
    qWarning() << "Expected" << sig << '(' << expectedPath << ')' << "but got" << sig << '(' << got << ')';
    return false;
  }
  return false;
}

bool KDirWatch_UnitTest::waitForRecreationSignal(KDirWatch &watch, const QString &path)
{
  // When watching for a deleted + created signal pair, the two might come so close that
  // using waitForOneSignal will miss the created signal.  This function monitors both all
  // the time to ensure both are received.

  const QString expectedPath = removeTrailingSlash(path);
  QSignalSpy spyDeleted(&watch, SIGNAL(deleted(QString)));
  QSignalSpy spyCreated(&watch, SIGNAL(created(QString)));

  if(!spyDeleted.wait(50 * s_maxTries)) {
    qWarning() << "Timeout waiting for KDirWatch signal deleted(QString) (" << path << ")";
    return false;
  }

  // Don't bother waiting for the created signal if the signal spy already received a signal.
  if(spyCreated.isEmpty() && !spyCreated.wait(50 * s_maxTries)) {
    qWarning() << "Timeout waiting for KDirWatch signal created(QString) (" << path << ")";
    return false;
  }

  return verifySignalPath(spyDeleted, "deleted(QString)", expectedPath) && verifySignalPath(spyCreated, "created(QString)", expectedPath);
}

QList<QVariantList> KDirWatch_UnitTest::waitForDeletedSignal(KDirWatch &watch, int expected)
{
    QSignalSpy spyDeleted(&watch, SIGNAL(created(QString)));
    int numTries = 0;
    // Give time for KDirWatch to notify us
    while (spyDeleted.count() < expected) {
        if (++numTries > s_maxTries) {
            qWarning() << "Timeout waiting for KDirWatch. Got" << spyDeleted.count() << "deleted() signals, expected" << expected;
            return spyDeleted;
        }
        QTest::qWait(50);
    }
    return spyDeleted;
}

void KDirWatch_UnitTest::touchOneFile() // watch a dir, create a file in it
{
    KDirWatch watch;
    watch.addDir(m_path);
    watch.startScan();

    waitUntilMTimeChange(m_path);

    // dirty(the directory) should be emitted.
    QSignalSpy spyCreated(&watch, SIGNAL(created(QString)));
    const QString file0 = createFile(0);
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), m_path));
    QCOMPARE(spyCreated.count(), 0); // "This is not emitted when creating a file is created in a watched directory."

    removeFile(0);
}

void KDirWatch_UnitTest::touch1000Files()
{
    KDirWatch watch;
    watch.addDir(m_path);
    watch.startScan();

    waitUntilMTimeChange(m_path);

    const int fileCount = 100;
    for (int i = 0; i < fileCount; ++i) {
        createFile(i);
    }

    QList<QVariantList> spy = waitForDirtySignal(watch, fileCount);
    if (watch.internalMethod() == KDirWatch::INotify) {
        QVERIFY(spy.count() >= fileCount);
        qDebug() << spy.count();
    } else {
        // More stupid backends just see one mtime change on the directory
        QVERIFY(spy.count() >= 1);
    }

    for (int i = 0; i < fileCount; ++i) {
        removeFile(i);
    }
}

void KDirWatch_UnitTest::watchAndModifyOneFile() // watch a specific file, and modify it
{
    KDirWatch watch;
    const QString existingFile = m_path + QLatin1String("ExistingFile");
    watch.addFile(existingFile);
    watch.startScan();
    if (m_slow) {
        waitUntilNewSecond();
    }
    appendToFile(existingFile);
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), existingFile));
}

void KDirWatch_UnitTest::removeAndReAdd()
{
    KDirWatch watch;
    watch.addDir(m_path);
    watch.startScan();
    if (watch.internalMethod() != KDirWatch::INotify) {
        waitUntilNewSecond();    // necessary for mtime checks in scanEntry
    }
    const QString file0 = createFile(0);
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), m_path));

    // Just like KDirLister does: remove the watch, then re-add it.
    watch.removeDir(m_path);
    watch.addDir(m_path);
    if (watch.internalMethod() != KDirWatch::INotify) {
        waitUntilMTimeChange(m_path);    // necessary for FAM and QFSWatcher
    }
    const QString file1 = createFile(1);
    //qDebug() << "created" << file1;
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), m_path));
}

void KDirWatch_UnitTest::watchNonExistent()
{
    KDirWatch watch;
    // Watch "subdir", that doesn't exist yet
    const QString subdir = m_path + QLatin1String("subdir");
    QVERIFY(!QFile::exists(subdir));
    watch.addDir(subdir);
    watch.startScan();

    if (m_slow) {
        waitUntilNewSecond();
    }

    // Now create it, KDirWatch should emit created()
    qDebug() << "Creating" << subdir;
    QDir().mkdir(subdir);

    QVERIFY(waitForOneSignal(watch, SIGNAL(created(QString)), subdir));

    KDirWatch::statistics();

    // Play with addDir/removeDir, just for fun
    watch.addDir(subdir);
    watch.removeDir(subdir);
    watch.addDir(subdir);

    // Now watch files that don't exist yet
    const QString file = subdir + QLatin1String("/0");
    watch.addFile(file); // doesn't exist yet
    const QString file1 = subdir + QLatin1String("/1");
    watch.addFile(file1); // doesn't exist yet
    watch.removeFile(file1); // forget it again

    KDirWatch::statistics();

    QVERIFY(!QFile::exists(file));
    // Now create it, KDirWatch should emit created
    qDebug() << "Creating" << file;
    createFile(file);
    QVERIFY(waitForOneSignal(watch, SIGNAL(created(QString)), file));

    appendToFile(file);
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), file));

    // Create the file after all; we're not watching for it, but the dir will emit dirty
    createFile(file1);
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), subdir));
}

void KDirWatch_UnitTest::watchNonExistentWithSingleton()
{
    const QString file = QLatin1String("/root/.ssh/authorized_keys");
    KDirWatch::self()->addFile(file);
    // When running this test in KDIRWATCH_METHOD=QFSWatch, or when FAM is not available
    // and we fallback to qfswatch when inotify fails above, we end up creating the fsWatch
    // in the kdirwatch singleton. Bug 261541 discovered that Qt hanged when deleting fsWatch
    // once QCoreApp was gone, this is what this test is about.
}

void KDirWatch_UnitTest::testDelete()
{
    const QString file1 = m_path + QLatin1String("del");
    if (!QFile::exists(file1)) {
        createFile(file1);
    }
    waitUntilMTimeChange(file1);

    // Watch the file, then delete it, KDirWatch will emit deleted (and possibly dirty for the dir, if mtime changed)
    KDirWatch watch;
    watch.addFile(file1);

    KDirWatch::statistics();

    QSignalSpy spyDirty(&watch, SIGNAL(dirty(QString)));
    QFile::remove(file1);
    QVERIFY(waitForOneSignal(watch, SIGNAL(deleted(QString)), file1));
    QTest::qWait(40); // just in case delayed processing would emit it
    QCOMPARE(spyDirty.count(), 0);
}

void KDirWatch_UnitTest::testDeleteAndRecreateFile() // Useful for /etc/localtime for instance
{
    const QString subdir = m_path + QLatin1String("subdir");
    QDir().mkdir(subdir);
    const QString file1 = subdir + QLatin1String("/1");
    if (!QFile::exists(file1)) {
        createFile(file1);
    }
    waitUntilMTimeChange(file1);

    // Watch the file, then delete it, KDirWatch will emit deleted (and possibly dirty for the dir, if mtime changed)
    KDirWatch watch;
    watch.addFile(file1);

    //KDE_struct_stat stat_buf;
    //QCOMPARE(KDE::stat(QFile::encodeName(file1), &stat_buf), 0);
    //qDebug() << "initial inode" << stat_buf.st_ino;

    // Make sure this even works multiple times, as needed for ksycoca
    for (int i = 0; i < 5; ++i) {

        QFile::remove(file1);
        // And recreate immediately, to try and fool KDirWatch with unchanged ctime/mtime ;)
        // (This emulates the /etc/localtime case)
        createFile(file1);

        //QCOMPARE(KDE::stat(QFile::encodeName(file1), &stat_buf), 0);
        //qDebug() << "new inode" << stat_buf.st_ino; // same!

        {
            QSignalSpy spyDirty(&watch, SIGNAL(dirty(QString)));
            if(!waitForRecreationSignal(watch, file1)) {
                // We may get a dirty signal here instead of a deleted/created set.
                if (spyDirty.isEmpty() || !verifySignalPath(spyDirty, SIGNAL(dirty(QString)), file1)) {
                    QFAIL("Failed to detect file deletion and recreation through either a deleted/created signal pair or through a dirty signal!");
                }
            }
        }
    }

    waitUntilMTimeChange(file1);

    appendToFile(file1);
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), file1));
}

void KDirWatch_UnitTest::testDeleteAndRecreateDir()
{
    // Like KDirModelTest::testOverwriteFileWithDir does at the end.
    // The linux-2.6.31 bug made kdirwatch emit deletion signals about the -new- dir!
    QTemporaryDir *tempDir1 = new QTemporaryDir(QDir::tempPath() + QLatin1Char('/') + QLatin1String("olddir-"));
    KDirWatch watch;
    const QString path1 = tempDir1->path() + QLatin1Char('/');
    watch.addDir(path1);

    delete tempDir1;
    QTemporaryDir *tempDir2 = new QTemporaryDir(QDir::tempPath() + QLatin1Char('/') + QLatin1String("newdir-"));
    const QString path2 = tempDir2->path() + QLatin1Char('/');
    watch.addDir(path2);

    QVERIFY(waitForOneSignal(watch, SIGNAL(deleted(QString)), path1));

    delete tempDir2;
}

void KDirWatch_UnitTest::testMoveTo()
{
    // This reproduces the famous digikam crash, #222974
    // A watched file was being rewritten (overwritten by ksavefile),
    // which gives inotify notifications "moved_to" followed by "delete_self"
    //
    // What happened then was that the delayed slotRescan
    // would adjust things, making it status==Normal but the entry was
    // listed as a "non-existent sub-entry" for the parent directory.
    // That's inconsistent, and after removeFile() a dangling sub-entry would be left.

    // Initial data: creating file subdir/1
    const QString file1 = m_path + QLatin1String("moveTo");
    createFile(file1);

    KDirWatch watch;
    watch.addDir(m_path);
    watch.addFile(file1);
    watch.startScan();

    if (watch.internalMethod() != KDirWatch::INotify) {
        waitUntilMTimeChange(m_path);
    }

    // Atomic rename of "temp" to "file1", much like KAutoSave would do when saving file1 again
    // ### TODO: this isn't an atomic rename anymore. We need ::rename for that, or API from Qt.
    const QString filetemp = m_path + QLatin1String("temp");
    createFile(filetemp);
    QFile::remove(file1);
    QVERIFY(QFile::rename(filetemp, file1)); // overwrite file1 with the tempfile
    qDebug() << "Overwrite file1 with tempfile";

    QSignalSpy spyCreated(&watch, SIGNAL(created(QString)));
    QSignalSpy spyDirty(&watch, SIGNAL(dirty(QString)));
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), m_path));

    // Getting created() on an unwatched file is an inotify bonus, it's not part of the requirements.
    if (watch.internalMethod() == KDirWatch::INotify) {
        QCOMPARE(spyCreated.count(), 1);
        QCOMPARE(spyCreated[0][0].toString(), file1);

        QCOMPARE(spyDirty.size(), 2);
        QCOMPARE(spyDirty[1][0].toString(), filetemp);
    }

    // make sure we're still watching it
    appendToFile(file1);
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), file1));

    //qDebug() << "after created";
    //KDirWatch::statistics();
    watch.removeFile(file1); // now we remove it
    //qDebug() << "after removeFile";
    //KDirWatch::statistics();

    // Just touch another file to trigger a findSubEntry - this where the crash happened
    waitUntilMTimeChange(m_path);
    createFile(filetemp);
#ifdef Q_OS_WIN
    if (watch.internalMethod() == KDirWatch::QFSWatch) {
        QEXPECT_FAIL(nullptr, "QFSWatch fails here on Windows!", Continue);
    }
#endif
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), m_path));
}

void KDirWatch_UnitTest::nestedEventLoop() // #220153: watch two files, and modify 2nd while in slot for 1st
{
    KDirWatch watch;

    const QString file0 = m_path + QLatin1String("nested_0");
    watch.addFile(file0);
    const QString file1 = m_path + QLatin1String("nested_1");
    watch.addFile(file1);
    watch.startScan();

    if (m_slow) {
        waitUntilNewSecond();
    }

    appendToFile(file0);

    // use own spy, to connect it before nestedEventLoopSlot, otherwise it reverses order
    QSignalSpy spyDirty(&watch, SIGNAL(dirty(QString)));
    connect(&watch, SIGNAL(dirty(QString)), this, SLOT(nestedEventLoopSlot()));
    waitForDirtySignal(watch, 1);
    QVERIFY(spyDirty.count() >= 2);
    QCOMPARE(spyDirty[0][0].toString(), file0);
    QCOMPARE(spyDirty[spyDirty.count() - 1][0].toString(), file1);
}

void KDirWatch_UnitTest::nestedEventLoopSlot()
{
    const KDirWatch *const_watch = qobject_cast<const KDirWatch *>(sender());
    KDirWatch *watch = const_cast<KDirWatch *>(const_watch);
    // let's not come in this slot again
    disconnect(watch, SIGNAL(dirty(QString)), this, SLOT(nestedEventLoopSlot()));

    const QString file1 = m_path + QLatin1String("nested_1");
    appendToFile(file1);
    //qDebug() << "now waiting for signal";
    // The nested event processing here was from a messagebox in #220153
    QList<QVariantList> spy = waitForDirtySignal(*watch, 1);
    QVERIFY(spy.count() >= 1);
    QCOMPARE(spy[spy.count() - 1][0].toString(), file1);
    //qDebug() << "done";

    // Now the user pressed reload...
    const QString file0 = m_path + QLatin1String("nested_0");
    watch->removeFile(file0);
    watch->addFile(file0);
}

void KDirWatch_UnitTest::testHardlinkChange()
{
#ifdef Q_OS_UNIX

    // The unittest for the "detecting hardlink change to /etc/localtime" problem
    // described on kde-core-devel (2009-07-03).
    // It shows that watching a specific file doesn't inform us that the file is
    // being recreated. Better watch the directory, for that.
    // Well, it works with inotify (and fam - which uses inotify I guess?)

    const QString existingFile = m_path + QLatin1String("ExistingFile");
    KDirWatch watch;
    watch.addFile(existingFile);
    watch.startScan();

    //waitUntilMTimeChange(existingFile);
    //waitUntilMTimeChange(m_path);

    QFile::remove(existingFile);
    const QString testFile = m_path + QLatin1String("TestFile");
    QVERIFY(::link(QFile::encodeName(testFile).constData(), QFile::encodeName(existingFile).constData()) == 0); // make ExistingFile "point" to TestFile
    QVERIFY(QFile::exists(existingFile));

    QVERIFY(waitForRecreationSignal(watch, existingFile));

    //KDirWatch::statistics();

    appendToFile(existingFile);
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), existingFile));
#else
    QSKIP("Unix-specific");
#endif
}

void KDirWatch_UnitTest::stopAndRestart()
{
    KDirWatch watch;
    watch.addDir(m_path);
    watch.startScan();

    waitUntilMTimeChange(m_path);

    watch.stopDirScan(m_path);

    //qDebug() << "create file 2 at" << QDateTime::currentDateTime().toTime_t();
    const QString file2 = createFile(2);
    QSignalSpy spyDirty(&watch, SIGNAL(dirty(QString)));
    QTest::qWait(200);
    QCOMPARE(spyDirty.count(), 0);// suspended -> no signal

    watch.restartDirScan(m_path);

    QTest::qWait(200);

#ifndef Q_OS_WIN
    QCOMPARE(spyDirty.count(), 0); // as documented by restartDirScan: no signal
    // On Windows, however, signals will get emitted, due to the ifdef Q_OS_WIN in the timestamp
    // comparison ("trust QFSW since the mtime of dirs isn't modified")
#endif

    KDirWatch::statistics();

    waitUntilMTimeChange(m_path); // necessary for the mtime comparison in scanEntry

    //qDebug() << "create file 3 at" << QDateTime::currentDateTime().toTime_t();
    const QString file3 = createFile(3);
#ifdef Q_OS_WIN
    if (watch.internalMethod() == KDirWatch::QFSWatch) {
        QEXPECT_FAIL(nullptr, "QFSWatch fails here on Windows!", Continue);
    }
#endif
    QVERIFY(waitForOneSignal(watch, SIGNAL(dirty(QString)), m_path));

    QFile::remove(file2);
    QFile::remove(file3);
}

void KDirWatch_UnitTest::shouldIgnoreQrcPaths()
{
    const auto oldCwd = QDir::currentPath();
    QVERIFY(QDir::setCurrent(QDir::homePath()));

    KDirWatch watch;
    watch.addDir(QDir::homePath());
    // This triggers bug #374075.
    watch.addDir(QStringLiteral(":/kio5/newfile-templates"));

    QSignalSpy dirtySpy(&watch, &KDirWatch::dirty);

    QFile file(QStringLiteral("bug374075.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QByteArrayLiteral("test")));
    file.close();
    QVERIFY(file.exists());
    QVERIFY(dirtySpy.wait());
    QVERIFY(dirtySpy.count() > 0);
    QVERIFY(file.remove());
    QVERIFY(QDir::setCurrent(oldCwd));
}

#include "kdirwatch_unittest.moc"
