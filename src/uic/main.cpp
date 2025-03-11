/*
 * Main for ui compiler for QSkinny.
*/
#include <QtCore/QCoreApplication>
#include <qskinny/QskGlobal.h>
#include <iostream>

#ifndef PROGNAME
#define PROGNAME qskuic
#endif
#define PROGNAME_STR QT_STRINGIFY(PROGNAME)

#ifndef VERSION
#define VERSION git
#endif
#define VERSION_STR QT_STRINGIFY(VERSION)

void showHelp(){
	std::cerr << "User interface compiler for QSkinny version " QSK_VERSION_STR ".\n"
	          << "Usage: " PROGNAME_STR " [options] <qskuic file> \n\n"
	          << " "
	          << std::endl;
}

/*
 * Start of the program. Drives the compilation.
*/
int main(int argn, char **argv){

	QCoreApplication app(argn, argv);
	QCoreApplication::setApplicationName(PROGNAME_STR);
	QCoreApplication::setApplicationVersion(VERSION_STR);

	showHelp();

	return 0;
}
