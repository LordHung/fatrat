# File generated by kdevelop's qmake manager. 
# ------------------------------------------- 
# Subdir relative project main directory: .
# Target is an application:  fatrat


FORMS += MainWindow.ui \
         QueueDlg.ui \
         WidgetHostDlg.ui \
         SettingsDlg.ui \
         SettingsGeneralForm.ui \
         ProxyDlg.ui \
         UserAuthDlg.ui \
         NewTransferDlg.ui \
 CommentForm.ui \
 SpeedLimitWidget.ui \
 AboutDlg.ui \
 engines/FtpUploadOptsForm.ui \
 engines/HttpOptsWidget.ui \
 engines/HttpUrlOptsDlg.ui \
 engines/SettingsTorrentForm.ui \
 engines/TorrentDetailsForm.ui \
 engines/TorrentOptsWidget.ui \
 tools/HashDlg.ui \
 tools/RapidTools.ui \
 tools/TorrentSearch.ui \
 GenericOptsForm.ui \
 engines/SettingsHttpForm.ui
TRANSLATIONS += locale/fatrat_cs_CZ.ts 
RESOURCES += gfx/resources.qrc 
HEADERS += fatrat.h \
           MainWindow.h \
           QueueDlg.h \
           QueueMgr.h \
           Queue.h \
           Transfer.h \
           TransfersModel.h \
           WidgetHostDlg.h \
           InfoBar.h \
           SettingsDlg.h \
           WidgetHostChild.h \
           SettingsGeneralForm.h \
           ProxyDlg.h \
           dbus_adaptor.h \
           SimpleEmail.h \
           UserAuthDlg.h \
           SpeedGraph.h \
 TransferLog.h \
 DropBox.h \
 NewTransferDlg.h \
 GenericOptsForm.h \
 CommentForm.h \
 QueueView.h \
 SpeedLimitWidget.h \
 AppTools.h \
 LimitedSocket.h \
 AboutDlg.h \
 MainTab.h \
 engines/FakeDownload.h \
 engines/FtpClient.h \
 engines/FtpUpload.h \
 engines/GeneralDownload.h \
 engines/HttpClient.h \
 engines/HttpFtpSettings.h \
 engines/RapidshareUpload.h \
 engines/SftpClient.h \
 engines/TorrentDownload.h \
 engines/TorrentFilesModel.h \
 engines/TorrentPeersModel.h \
 engines/TorrentPiecesModel.h \
 engines/TorrentProgressWidget.h \
 engines/TorrentSettings.h \
 tools/HashDlg.h \
 tools/RapidTools.h \
 tools/TorrentSearch.h \
 remote/HttpService.h \
 remote/GenericService.h \
 remote/XmlService.h
SOURCES += fatrat.cpp \
           MainWindow.cpp \
           QueueMgr.cpp \
           Queue.cpp \
           Transfer.cpp \
           TransfersModel.cpp \
           InfoBar.cpp \
           dbus_adaptor.cpp \
           SimpleEmail.cpp \
           SpeedGraph.cpp \
 DropBox.cpp \
 QueueView.cpp \
 NewTransferDlg.cpp \
 AppTools.cpp \
 SpeedLimitWidget.cpp \
 LimitedSocket.cpp \
 MainTab.cpp \
 engines/FtpClient.cpp \
 engines/FtpUpload.cpp \
 engines/GeneralDownload.cpp \
 engines/HttpClient.cpp \
 engines/HttpFtpSettings.cpp \
 engines/RapidshareUpload.cpp \
 engines/SftpClient.cpp \
 engines/TorrentDownload.cpp \
 engines/TorrentFilesModel.cpp \
 engines/TorrentPeersModel.cpp \
 engines/TorrentPiecesModel.cpp \
 engines/TorrentProgressWidget.cpp \
 tools/HashDlg.cpp \
 tools/RapidTools.cpp \
 tools/TorrentSearch.cpp \
 remote/HttpService.cpp \
 remote/GenericService.cpp \
 remote/XmlService.cpp
TEMPLATE = app
TARGET = fatrat
CONFIG += qdbus debug
QT += xml network
LIBS += -ltorrent -ldl -lssh2
INCLUDEPATH = /usr/include/libtorrent

!exists( /usr/include/libtorrent/session.hpp ){
    error("You need Rasterbar libtorrent ***0.13*** (unreleased dev version) to compile this program - http://www.rasterbar.com/products/libtorrent/")
}

!exists( /usr/include/libssh2.h ){
    error("You need the libssh2 library - http://www.libssh2.org")
}

INSTALL_ROOT = /usr/bin

locale.files = locale/*.qm 
locale.path = /usr/share/fatrat/lang
INSTALLS += locale

other_data.files = data/btsearch.xml data/LICENSE.txt data/TRANSLATIONS.txt data/3RDPARTIES.txt
other_data.path = /usr/share/fatrat/data
INSTALLS += other_data

icons.files = gfx/fatrat.png
icons.path = /usr/share/pixmaps
INSTALLS += icons

menu.files = data/fatrat.desktop
menu.path = /usr/share/applications
INSTALLS += menu

target.path = /usr/bin

INSTALLS += target
