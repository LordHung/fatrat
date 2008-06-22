/*
FatRat download manager
http://fatrat.dolezel.info

Copyright (C) 2006-2008 Lubos Dolezel <lubos a dolezel.info>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
version 2 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "SettingsQueueForm.h"
#include <QSettings>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include "Settings.h"

extern QSettings* g_settings;

SettingsQueueForm::SettingsQueueForm(QWidget* w, QObject* parent) : QObject(parent)
{
	setupUi(w);
}

void SettingsQueueForm::load()
{
	spinRetry->setValue( getSettingsValue("retrycount").toInt() );
	checkRetryWorking->setChecked( getSettingsValue("retryworking").toBool() );
	checkPopup->setChecked( getSettingsValue("showpopup").toBool() );
	spinPopup->setValue( getSettingsValue("popuptime").toInt() );
	checkEmail->setChecked( getSettingsValue("sendemail").toBool() );
	lineSmtp->setText( getSettingsValue("smtpserver").toString() );
	lineSender->setText( getSettingsValue("emailsender").toString() );
	lineRecipient->setText( getSettingsValue("emailrcpt").toString() );
	
	checkPopup->setEnabled(QSystemTrayIcon::supportsMessages());
	checkAutoRemove->setChecked( getSettingsValue("autoremove").toBool() );
}

void SettingsQueueForm::accepted()
{
	g_settings->setValue("retrycount", spinRetry->value());
	g_settings->setValue("retryworking", checkRetryWorking->isChecked());
	g_settings->setValue("showpopup", checkPopup->isChecked());
	g_settings->setValue("popuptime", spinPopup->value());
	g_settings->setValue("sendemail", checkEmail->isChecked());
	g_settings->setValue("smtpserver", lineSmtp->text());
	g_settings->setValue("emailsender", lineSender->text());
	g_settings->setValue("emailrcpt", lineRecipient->text());
	g_settings->setValue("autoremove", checkAutoRemove->isChecked());
}

bool SettingsQueueForm::accept()
{
	if(!lineSender->text().contains('@') || !lineRecipient->text().contains('@'))
	{
		QMessageBox::critical(0, tr("Error"), tr("The e-mail address is incorrect."));
		return false;
	}
	return true;
}