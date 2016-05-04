/*
 *  Copyright 2016 Friedrich W. H. Kossebau  <kossebau@kde.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License version 2 as published by the Free Software Foundation;
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

// test object
#include <kaboutdata.h>
// Qt
#include <QObject>
#include <QTest>

// Separate test for reading & setting applicationData
// to ensure a separate process where no other test case has
// directly or indirectly called KAboutData::setApplicationData before
// and thus created the global KAboutData object
class KAboutDataApplicationDataTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testInteractionWithQApplicationData();
};


static const char AppName[] =            "app";
static const char ProgramName[] =        "ProgramName";
static const char Version[] =            "Version";
static const char OrganizationDomain[] = "no.where";
static const char DesktopFileName[] =    "org.kde.someapp";

static const char AppName2[] =            "otherapp";
static const char ProgramName2[] =        "OtherProgramName";
static const char Version2[] =            "OtherVersion";
static const char OrganizationDomain2[] = "other.no.where";
static const char DesktopFileName2[] =    "org.kde.otherapp";

void KAboutDataApplicationDataTest::testInteractionWithQApplicationData()
{
    // init the app metadata the Qt way
    QCoreApplication *app = QCoreApplication::instance();
    app->setApplicationName(QLatin1String(AppName));
    app->setProperty("applicationDisplayName", QLatin1String(ProgramName));
    app->setApplicationVersion(QLatin1String(Version));
    app->setOrganizationDomain(QLatin1String(OrganizationDomain));
    app->setProperty("desktopFileName", QLatin1String(DesktopFileName));

    // without setting before, get KAboutData::applicationData
    const KAboutData applicationAboutData = KAboutData::applicationData();

    // should be initialized with Q*Application metadata
    QCOMPARE(applicationAboutData.componentName(), QLatin1String(AppName));
    QCOMPARE(applicationAboutData.displayName(), QLatin1String(ProgramName));
    QCOMPARE(applicationAboutData.organizationDomain(), QLatin1String(OrganizationDomain));
    QCOMPARE(applicationAboutData.version(), QLatin1String(Version));
    QCOMPARE(applicationAboutData.desktopFileName(), QLatin1String(DesktopFileName));

    // now set some new KAboutData, with different values
    KAboutData aboutData2(AppName2, QLatin1String(ProgramName2), Version2);
    aboutData2.setOrganizationDomain(OrganizationDomain2);
    aboutData2.setDesktopFileName(QLatin1String(DesktopFileName2));

    KAboutData::setApplicationData(aboutData2);

    // check that Q*Application metadata has been updated, as expected per API definition
    QCOMPARE(app->applicationName(), QLatin1String(AppName2));
    QCOMPARE(app->property("applicationDisplayName").toString(), QLatin1String(ProgramName2));
    QCOMPARE(app->organizationDomain(), QLatin1String(OrganizationDomain2));
    QCOMPARE(app->applicationVersion(), QLatin1String(Version2));
    QCOMPARE(app->property("desktopFileName").toString(), QLatin1String(DesktopFileName2));

    // and check as well KAboutData::applicationData itself
    const KAboutData applicationAboutData2 = KAboutData::applicationData();

    QCOMPARE(applicationAboutData2.componentName(), QLatin1String(AppName2));
    QCOMPARE(applicationAboutData2.displayName(), QLatin1String(ProgramName2));
    QCOMPARE(applicationAboutData2.organizationDomain(), QLatin1String(OrganizationDomain2));
    QCOMPARE(applicationAboutData2.version(), QLatin1String(Version2));
    QCOMPARE(applicationAboutData2.desktopFileName(), QLatin1String(DesktopFileName2));
}

QTEST_MAIN(KAboutDataApplicationDataTest)

#include "kaboutdataapplicationdatatest.moc"
