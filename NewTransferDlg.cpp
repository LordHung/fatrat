#include "NewTransferDlg.h"
#include "fatrat.h"
#include "Queue.h"
#include "UserAuthDlg.h"
#include <QFileDialog>
#include <QSettings>
#include <QReadWriteLock>
#include <QMenu>
#include <QClipboard>
#include <QtDebug>

extern QSettings* g_settings;
extern QList<Queue*> g_queues;
extern QReadWriteLock g_queuesLock;

NewTransferDlg::NewTransferDlg(QWidget* parent)
: QDialog(parent), m_bDetails(false), m_bPaused(false), m_nDownLimit(0),
		m_nUpLimit(0), m_nClass(-1), m_mode(Transfer::Download),
		m_nQueue(0)
{
	setupUi(this);

	textURIs->setFocus(Qt::OtherFocusReason);
	spinDown->setRange(0,INT_MAX);
	spinUp->setRange(0,INT_MAX);
	
	connect(toolBrowse, SIGNAL(pressed()), this, SLOT(browse()));
	connect(radioDownload, SIGNAL(clicked()), this, SLOT(switchMode()));
	connect(radioUpload, SIGNAL(clicked()), this, SLOT(switchMode()));
	connect(pushAddFiles, SIGNAL(clicked()), this, SLOT(browse2()));
	connect(toolAuth, SIGNAL(clicked()), this, SLOT(authData()));
	connect(toolAuth2, SIGNAL(clicked()), this, SLOT(authData()));
	
	const EngineEntry* entries = Transfer::engines(Transfer::Download);
	comboClass->addItem(tr("Auto detect"));
	
	for(int i=0;entries[i].shortName;i++)
		comboClass->addItem(entries[i].longName);
	
	entries = Transfer::engines(Transfer::Upload);
	comboClass2->addItem(tr("Auto detect"));
	
	for(int i=0;entries[i].shortName;i++)
		comboClass2->addItem(entries[i].longName);
	
	QMenu* menu = new QMenu(toolAddSpecial);
	QAction* act;
	
	act = menu->addAction(tr("Add local files..."));
	connect(act, SIGNAL(triggered()), this, SLOT(browse2()));
	
	act = menu->addAction(tr("Add contents of a text file..."));
	connect(act, SIGNAL(triggered()), this, SLOT(addTextFile()));
	
	act = menu->addAction(tr("Add from clipboard"));
	connect(act, SIGNAL(triggered()), this, SLOT(addClipboard()));
	
	toolAddSpecial->setMenu(menu);
}

int NewTransferDlg::exec()
{
	int result;
	
	load();
	result = QDialog::exec();
	
	if(result == QDialog::Accepted)
		accepted();
	
	return result;
}

void NewTransferDlg::accept()
{
	if(radioDownload->isChecked())
	{
		if((!m_bNewTransfer || !textURIs->toPlainText().isEmpty()) && !comboDestination->currentText().isEmpty())
		{
			QDir dir(comboDestination->currentText());
			if(dir.isReadable())
			{
				QDialog::accept();
				return;
			}
		}
	}
	else
		QDialog::accept();
}
void NewTransferDlg::accepted()
{
	m_mode = radioDownload->isChecked() ? Transfer::Download : Transfer::Upload;
	if(m_mode == Transfer::Download)
	{
		m_strURIs = textURIs->toPlainText();
		m_strDestination = comboDestination->currentText();
		m_nClass = comboClass->currentIndex()-1;
		
		if(!m_lastDirs.contains(m_strDestination) && !m_strDestination.isEmpty())
		{
			if(m_lastDirs.size() >= 5)
				m_lastDirs.removeFirst();
			m_lastDirs.append(m_strDestination);
			g_settings->setValue("lastdirs", m_lastDirs);
			g_settings->sync();
		}
	}
	else
	{
		m_strURIs = textFiles->toPlainText();
		m_strDestination = comboDestination2->currentText();
		m_nClass = comboClass2->currentIndex()-1;
	}
	
	m_bDetails = checkDetails->isChecked();
	m_bPaused = checkPaused->isChecked();
	m_nDownLimit = spinDown->value()*1024;
	m_nUpLimit = spinUp->value()*1024;
	
	m_nQueue = comboQueue->currentIndex();
}

void NewTransferDlg::load()
{
	/*if(!m_strClass.isEmpty())
	{
		if(m_lastDirs.size() >= 5)
			m_lastDirs.removeFirst();
		m_lastDirs.append(m_strDestination);
		g_settings->setValue("lastdirs", m_lastDirs);
	}*/
	
	{
		bool bFound = false;
		const EngineEntry* entries;
		
		entries = Transfer::engines(Transfer::Download);
		
		for(int i=0;entries[i].shortName;i++)
		{
			if(m_strClass == entries[i].shortName)
			{
				comboClass->setCurrentIndex(i+1);
				bFound = true;
				break;
			}
		}
		
		if(!bFound)
		{
			entries = Transfer::engines(Transfer::Upload);
		
			for(int i=0;entries[i].shortName;i++)
			{
				if(m_strClass == entries[i].shortName)
				{
					comboClass2->setCurrentIndex(i+1);
					break;
				}
			}
		}
	}
	{
		g_queuesLock.lockForRead();
		
		foreach(Queue* q, g_queues)
			comboQueue->addItem(q->name());
		
		comboQueue->setCurrentIndex(m_nQueue);
		
		g_queuesLock.unlock();
	}
	
	if(m_strDestination.isEmpty())
		m_strDestination = g_settings->value("defaultdir", getSettingsDefault("defaultdir")).toString();
	
	m_lastDirs = g_settings->value("lastdirs").toStringList();
	if(!m_lastDirs.contains(m_strDestination))
		comboDestination->addItem(m_strDestination);
	comboDestination->addItems(m_lastDirs);
	comboDestination->setEditText(m_strDestination);
	
	spinDown->setValue(m_nDownLimit/1024);
	spinUp->setValue(m_nUpLimit/1024);
	checkDetails->setChecked(m_bDetails);
	checkPaused->setChecked(m_bPaused);
	
	if(m_mode == Transfer::Upload)
	{
		stackedWidget->setCurrentIndex(1);
		radioUpload->setChecked(true);
		textFiles->setText(m_strURIs);
	}
	else
		textURIs->setText(m_strURIs);
}

void NewTransferDlg::addTextFile()
{
	QString filename = QFileDialog::getOpenFileName(this, "FatRat");
	if(!filename.isNull())
	{
		QFile file(filename);
		if(file.open(QIODevice::ReadOnly))
			textURIs->append(file.readAll());
	}
}

void NewTransferDlg::addClipboard()
{
	QClipboard* cb = QApplication::clipboard();
	if(radioUpload->isChecked())
		textFiles->append(cb->text());
	else
		textURIs->append(cb->text());
}

void NewTransferDlg::browse()
{
	QString dir = QFileDialog::getExistingDirectory(this, "FatRat", comboDestination->currentText());
	if(!dir.isNull())
		comboDestination->setEditText(dir);
}

void NewTransferDlg::browse2()
{
	QStringList files;
	
	files = QFileDialog::getOpenFileNames(this, "FatRat");
	
	if(radioUpload->isChecked())
		textFiles->append(files.join("\n"));
	else
		textURIs->append(files.join("\n"));
}

/*
void NewTransferDlg::textChanged()
{
	QStringList list;
	
	if(radioDownload->isChecked())
		list = textURIs->toPlainText().split(QRegExp('\n'), QString::SkipEmptyParts);
	else
		list = textFiles->toPlainText().split(QRegExp('\n'), QString::SkipEmptyParts);
	checkDetails->setEnabled(list.size() <= 1);
}
*/

void NewTransferDlg::switchMode()
{
	if(radioUpload->isChecked())
	{
		stackedWidget->setCurrentIndex(1);
		textFiles->setText( textURIs->toPlainText() );
	}
	else
	{
		stackedWidget->setCurrentIndex(0);
		textURIs->setText( textFiles->toPlainText() );
	}
}

void NewTransferDlg::authData()
{
	UserAuthDlg dlg(false, this);
	dlg.m_auth = m_auth;
	
	if(dlg.exec() == QDialog::Accepted)
		m_auth = dlg.m_auth;
}

