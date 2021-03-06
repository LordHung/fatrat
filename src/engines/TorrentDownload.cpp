/*
FatRat download manager
http://fatrat.dolezel.info

Copyright (C) 2006-2010 Lubos Dolezel <lubos a dolezel.info>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
version 3 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.

In addition, as a special exemption, Luboš Doležel gives permission
to link the code of FatRat with the OpenSSL project's
"OpenSSL" library (or with modified versions of it that use the; same
license as the "OpenSSL" library), and distribute the linked
executables. You must obey the GNU General Public License in all
respects for all of the code used other than "OpenSSL".
*/

#include "fatrat.h"
#include "config.h"
#include "Logger.h"
#include "Settings.h"
#include "Queue.h"
#include "TorrentDownload.h"
#include "TorrentSettings.h"
#include "TorrentDetails.h"
#include "TorrentOptsWidget.h"
#include "RuntimeException.h"
#include "rss/RssFetcher.h"
#include "TorrentProgressWidget.h"

#include <libtorrent/bencode.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/extensions/metadata_transfer.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/session_status.hpp>

#include <fstream>
#include <stdexcept>
#include <memory>

#include <QIcon>
#include <QMenu>
#include <QLibrary>
#include <QDir>
#include <QUrl>
#include <QFile>
#include <QVector>
#include <QLabel>
#include <QBuffer>
#include <QImage>
#include <QtDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#ifdef WITH_WEBINTERFACE
#	define XMLRPCSERVICE_AVOID_SHA_CONFLICT
#	include "remote/XmlRpcService.h"
#	include "Queue.h"

	extern QReadWriteLock g_queuesLock;
#endif

#if (!defined(NDEBUG) && !defined(DEBUG))
#	error Define NDEBUG or DEBUG!
#endif
#if defined(NDEBUG)
#	warning NDEBUG defined - please always make sure that libtorrent also has NDEBUG defined
#else
#	warning DEBUG defined - please always make sure that libtorrent also has DEBUG defined
#endif

libtorrent::session* TorrentDownload::m_session = 0;
TorrentWorker* TorrentDownload::m_worker = 0;
bool TorrentDownload::m_bDHT = false;
QList<QRegExp> TorrentDownload::m_listBTLinks;
QLabel* TorrentDownload::m_labelDHTStats = 0;
QMutex TorrentDownload::m_mutexAlerts;

const char* TORRENT_FILE_STORAGE = ".local/share/fatrat/torrents";
const char* MAGNET_PREFIX = "magnet:?xt=urn:btih:";

void* g_pGeoIP = 0;
QLibrary g_geoIPLib;
void* (*GeoIP_new_imp)(int);
const char* (*GeoIP_country_name_by_addr_imp)(void*, const char*);
const char* (*GeoIP_country_code_by_addr_imp)(void*, const char*);
void (*GeoIP_delete_imp)(void*);

TorrentDownload::TorrentDownload(bool bAuto)
	:  m_info(0), m_bHasHashCheck(false), m_bAuto(bAuto), m_bSuperSeeding(false), m_pFileDownload(0)
		, m_pFileDownloadTemp(0)
{
	m_worker->addObject(this);
}

TorrentDownload::~TorrentDownload()
{
	m_worker->removeObject(this);
	if(m_handle.is_valid())
		m_session->remove_torrent(m_handle);
	//delete m_info;
}

int TorrentDownload::acceptable(QString uri, bool)
{
	bool istorrent;
	
	if(uri.endsWith(".torrent", Qt::CaseInsensitive))
		istorrent = true;
	else
	{
		istorrent = false;
		
		for(int i=0;i<m_listBTLinks.size();i++)
		{
			if(m_listBTLinks[i].exactMatch(uri))
			{
				istorrent = true;
				break;
			}
			else
				qDebug() << m_listBTLinks[i].pattern() << "not matched by" << uri;
		}
	}
		
        if(uri.startsWith("http://") || uri.startsWith("ftp://"))
                return (istorrent) ? 3 : 1;
	if(istorrent)
	{
		if(uri[0] == '/' || uri.startsWith("file://"))
			return 2;
	}
	if(uri.startsWith(MAGNET_PREFIX))
		return 3;
        return 0;
}

void TorrentDownload::globalInit()
{
	QString ua = getSettingsValue("torrent/ua").toString();
	short s1 = 0, s2 = 0, s3 = 0, s4 = 0;
	QRegExp reVersion("(\\d)\\.(\\d)\\.(\\d)\\.?(\\d)?");
	
	if(reVersion.indexIn(ua) != -1)
	{
		QStringList caps = reVersion.capturedTexts();
		s1 = caps[1].toShort();
		s2 = caps[2].toShort();
		s3 = caps[3].toShort();
		if(caps.size() >= 5)
			s4 = caps[4].toShort();
	}
	
	libtorrent::fingerprint fp = libtorrent::fingerprint("FR", s1, s2, s3, s4);
	if(ua.startsWith(QString::fromUtf8("μTorrent")))
		fp = libtorrent::fingerprint("UT", s1, s2, s3, s4);
	else if(ua.startsWith("Azureus"))
		fp = libtorrent::fingerprint("AZ", s1, s2, s3, s4);
	
	int lstart,lend;
	
	lstart = getSettingsValue("torrent/listen_start").toInt();
	lend = getSettingsValue("torrent/listen_end").toInt();
	
	if(lend < lstart)
		lend = lstart;
	
	m_session = new libtorrent::session(fp, std::pair<int,int>(lstart,lend));
	m_session->set_alert_mask(libtorrent::alert::all_categories);
	
	if(programHasGUI())
		m_labelDHTStats = new QLabel;
	
	applySettings();
	
	if(getSettingsValue("torrent/pex").toBool())
		m_session->add_extension(&libtorrent::create_ut_pex_plugin);

	// Deprecated. The replacement is added by default.
	// m_session->add_extension(&libtorrent::create_metadata_plugin);
	m_session->add_extension(&libtorrent::create_ut_metadata_plugin);
	m_session->add_extension(&libtorrent::create_smart_ban_plugin);
	
	m_worker = new TorrentWorker;
	
	g_geoIPLib.setFileName("libGeoIP");
	if(g_geoIPLib.load())
	{
		GeoIP_new_imp = (void*(*)(int)) g_geoIPLib.resolve("GeoIP_new");
		GeoIP_country_name_by_addr_imp = (const char*(*)(void*, const char*)) g_geoIPLib.resolve("GeoIP_country_name_by_addr");
		GeoIP_country_code_by_addr_imp = (const char*(*)(void*, const char*)) g_geoIPLib.resolve("GeoIP_country_code_by_addr");
		GeoIP_delete_imp = (void(*)(void*)) g_geoIPLib.resolve("GeoIP_delete");
		
		g_pGeoIP = GeoIP_new_imp(1 /*GEOIP_MEMORY_CACHE*/);
	}
	
	QFile file;
	if(openDataFile(&file, "/data/btlinks.txt"))
	{
		while(!file.atEnd())
		{
			QByteArray line = file.readLine().trimmed();
			if(line.isEmpty())
				continue;
			m_listBTLinks << QRegExp(line);
		}
	}
	
	SettingsItem si;
	
	si.icon = DelayedIcon(":/fatrat/bittorrent.png");
	si.title = tr("BitTorrent");
	si.lpfnCreate = TorrentSettings::create;
	si.pfnApply = TorrentSettings::applySettings;

#ifdef WITH_WEBINTERFACE
	// register XML-RPC functions
	XmlRpcService::registerFunction("TorrentDownload.setFilePriorities", setFilePriorities,
					QVector<QVariant::Type>() << QVariant::String << QVariant::Map);

	si.webSettingsScript = "/scripts/settings/bittorrent.js";
	si.webSettingsIconURL = "/img/settings/bittorrent.png";
#endif
	
	addSettingsPage(si);
}

void TorrentDownload::applySettings()
{
	static bool bUPnPActive = false, bNATPMPActive = false, bLSDActive = false;
	bool bUPnP, bNATPMP, bLSD;
	int lstart,lend;
	libtorrent::session_settings settings;
	
	lstart = getSettingsValue("torrent/listen_start").toInt();
	lend = getSettingsValue("torrent/listen_end").toInt();
	
	if(lend < lstart)
		lend = lstart;
	
	if(m_session->listen_port() != lstart)
	{
		libtorrent::error_code ec; // TODO: Do we use this in any way?
		m_session->listen_on(std::pair<int,int>(lstart,lend), ec);
	}
	
	bUPnP = getSettingsValue("torrent/mapping_upnp").toBool();
	bNATPMP = getSettingsValue("torrent/mapping_natpmp").toBool();
	bLSD = getSettingsValue("torrent/mapping_lsd").toBool();
	
	if(!bUPnP)
		m_session->stop_upnp(); // libtorrent bug workaround
	if(bUPnP != bUPnPActive)
	{
		if(bUPnP)
			m_session->start_upnp();
		//else
		//	m_session->stop_upnp();
		bUPnPActive = bUPnP;
	}
	if(!bNATPMP) // libtorrent bug workaround
		m_session->stop_natpmp();
	if(bNATPMP != bNATPMPActive)
	{
		if(bNATPMP)
			m_session->start_natpmp();
		//else
		//	m_session->stop_natpmp();
		bNATPMPActive = bNATPMP;
	}
	if(!bLSD) // libtorrent bug workaround
		m_session->stop_lsd();
	if(bLSD != bLSDActive)
	{
		if(bLSD)
			m_session->start_lsd();
		//else
		//	m_session->stop_lsd();
		bLSDActive = bLSD;
	}
	
	if(getSettingsValue("torrent/dht").toBool())
	{
		QByteArray state = getSettingsValue("torrent/dht_state").toByteArray();
		while(!m_bDHT)
		{
			try
			{
#ifdef LIBTORRENT_0_15
				libtorrent::lazy_entry e;
				lazy_bdecode_simple(e, state);
				m_session->load_state(e);
				m_session->start_dht();
#else
				m_session->start_dht(bdecode_simple(state));
#endif
				m_session->add_dht_router(std::pair<std::string, int>("router.bittorrent.com", 6881));
				m_bDHT = true;
				
				Logger::global()->enterLogMessage("BitTorrent", tr("DHT started"));
			}
			catch(const std::exception& e)
			{
				m_bDHT = false;
				Logger::global()->enterLogMessage("BitTorrent", tr("Failed to start DHT!") + ' ' + e.what());
				
				if(!state.isEmpty())
				{
					state.clear();
					continue;
				}
				break;
			}
		}
		
		if(programHasGUI())
			addStatusWidget(m_labelDHTStats, true);
	}
	else //if(m_bDHT)
	{
		m_session->stop_dht();
		m_bDHT = false;
		
		if(programHasGUI())
			removeStatusWidget(m_labelDHTStats);
	}
	
	QString ua = getSettingsValue("torrent/ua").toString();
	ua.replace("%v", VERSION);
	
	settings.file_pool_size = getSettingsValue("torrent/maxfiles").toInt();
	settings.use_dht_as_fallback = false; // i.e. use DHT always
	settings.user_agent = ua.toStdString();
	//settings.max_out_request_queue = 300;
	//settings.piece_timeout = 50;
	settings.unchoke_slots_limit = getSettingsValue("torrent/maxuploads").toInt();
	settings.connections_limit = getSettingsValue("torrent/maxconnections").toInt();
	settings.max_failcount = 7;
	settings.request_queue_time = 30.f;
	settings.max_out_request_queue = 100;
	settings.cache_size = getSettingsValue("torrent/cache_size").toInt();
	settings.disk_io_write_mode = getSettingsValue("torrent/disk_io_write_mode").toInt();
	settings.disk_io_read_mode = getSettingsValue("torrent/disk_io_read_mode").toInt();

	QString external_ip = getSettingsValue("torrent/external_ip").toString();
	if(!external_ip.isEmpty())
		settings.announce_ip = external_ip.toStdString();
	
	m_session->set_settings(settings);
	
	libtorrent::pe_settings ps;
	
	switch(getSettingsValue("torrent/enc_incoming").toInt())
	{
	case 0:
		ps.in_enc_policy = libtorrent::pe_settings::disabled; break;
	case 1:
		ps.in_enc_policy = libtorrent::pe_settings::enabled; break;
	case 2:
		ps.in_enc_policy = libtorrent::pe_settings::forced; break;
	}
	switch(getSettingsValue("torrent/enc_outgoing").toInt())
	{
	case 0:
		ps.out_enc_policy = libtorrent::pe_settings::disabled; break;
	case 1:
		ps.out_enc_policy = libtorrent::pe_settings::enabled; break;
	case 2:
		ps.out_enc_policy = libtorrent::pe_settings::forced; break;
	}
	
	switch(getSettingsValue("torrent/enc_level").toInt())
	{
	case 0:
		ps.allowed_enc_level = libtorrent::pe_settings::plaintext; break;
	case 1:
		ps.allowed_enc_level = libtorrent::pe_settings::rc4; break;
	case 2:
		ps.allowed_enc_level = libtorrent::pe_settings::both;
	}
	
	ps.prefer_rc4 = getSettingsValue("torrent/enc_rc4_prefer").toBool();
	m_session->set_pe_settings(ps);
	
	// Proxy settings
	QUuid proxy;
	libtorrent::proxy_settings ltproxy;
	
	proxy = getSettingsValue("torrent/proxy").toString();
	
	ltproxy = proxyToLibtorrent(Proxy::getProxy(proxy));
	m_session->set_proxy(ltproxy);
}

libtorrent::proxy_settings TorrentDownload::proxyToLibtorrent(Proxy p)
{
	libtorrent::proxy_settings s;
	
	if(p.nType != Proxy::ProxyNone)
	{
		s.hostname = p.strIP.toStdString();
		s.port = p.nPort;
		
		bool bAuth = !p.strUser.isEmpty();
		if(bAuth)
		{
			s.username = p.strUser.toStdString();
			s.password = p.strPassword.toStdString();
		}
		
		if(p.nType == Proxy::ProxyHttp)
		{
			if(bAuth)
				s.type = libtorrent::proxy_settings::http_pw;
			else
				s.type = libtorrent::proxy_settings::http;
		}
		else if(p.nType == Proxy::ProxySocks5)
		{
			if(bAuth)
				s.type = libtorrent::proxy_settings::socks5_pw;
			else
				s.type = libtorrent::proxy_settings::socks5;
		}
	}
	
	return s;
}

void TorrentDownload::globalExit()
{
	if(m_bDHT)
	{
		libtorrent::entry e;
#ifdef LIBTORRENT_0_15
		m_session->save_state(e, libtorrent::session::save_dht_state);
#else
		e = m_session->dht_state();
#endif
		setSettingsValue("torrent/dht_state", bencode_simple(e));
	}

	// Without this the process could freeze for up to 60 seconds
	libtorrent::session_settings s;
	s.tracker_completion_timeout = s.tracker_receive_timeout = 5;
	m_session->set_settings(s);

	m_session->abort();

	delete m_worker;
	delete m_session;
	
	if(g_pGeoIP != 0)
		GeoIP_delete_imp(g_pGeoIP);
}

QString TorrentDownload::name() const
{
	if(m_handle.is_valid())
	{
		libtorrent::torrent_status st;

		st = m_handle.status(libtorrent::torrent_handle::query_name);

		if(st.name.empty() && m_status.state == libtorrent::torrent_status::downloading_metadata)
			return tr("Downloading metadata: %1%").arg((int) m_status.progress*100);
		else
			return QString::fromUtf8(st.name.c_str());
	}
	else if(m_pFileDownload != 0)
		return tr("Downloading the .torrent file...");
	else
		return "*INVALID*";
}

QString TorrentDownload::dataPath(bool bDirect) const
{
	if (!m_handle.is_valid())
		return QString();
	else
		return Transfer::dataPath(bDirect);
}

void TorrentDownload::createDefaultPriorityList()
{
	int numFiles = m_info->num_files();
	m_vecPriorities.assign(numFiles, 1);
}

void TorrentDownload::init(QString source, QString target)
{
	m_strTarget = target;
	
	m_seedLimitRatio = getSettingsValue("torrent/maxratio").toDouble();
	m_seedLimitUpload = 0;
	
	try
	{
		if(source.startsWith("file://"))
			source = source.mid(7);
		
		bool localFile = source.startsWith('/');
		bool magnetLink = source.startsWith(MAGNET_PREFIX);
		
		if(localFile || magnetLink)
		{
			libtorrent::storage_mode_t storageMode;
			storageMode = (libtorrent::storage_mode_t) getSettingsValue("torrent/allocation").toInt();
			
			if(localFile)
			{
				QFile in(source);
				//QByteArray data;
				//const char* p;
				
				if(!in.open(QIODevice::ReadOnly))
					throw RuntimeException(tr("Unable to open the file!"));
				
				//data = in.readAll();
				//p = data.data();
				
				libtorrent::add_torrent_params params;
				boost::shared_ptr<libtorrent::torrent_info> ti(new libtorrent::torrent_info(source.toStdString()));

				m_info = ti;
				
				params.ti = ti;
				params.save_path = target.toStdString();
				params.storage_mode = storageMode;
				params.paused = !isActive();
				params.auto_managed = false;
				params.flags = libtorrent::add_torrent_params::flag_duplicate_is_error;

				if (!isActive())
					params.flags |= libtorrent::add_torrent_params::flag_paused;
				
				m_handle = m_session->add_torrent(params);
				//m_handle = m_session->add_torrent(m_info, target.toStdString(), libtorrent::entry(), storageMode, !isActive());
			}
			else
			{
				libtorrent::add_torrent_params params;
				QByteArray path = source.toUtf8();
				std::string ss = path.constData();

				params.name = ss.c_str();
				path = target.toUtf8();
				params.save_path = path.constData();
				params.storage_mode = storageMode;
				params.paused = !isActive();
				params.auto_managed = false;
				params.url = ss;
				params.flags = libtorrent::add_torrent_params::flag_duplicate_is_error;

				if (!isActive())
					params.flags |= libtorrent::add_torrent_params::flag_paused;

				m_handle = m_session->add_torrent(params);
			}
			
			
			{
				int limit;
				
				limit = getSettingsValue("torrent/maxuploads_loc").toInt();
				m_handle.set_max_uploads(limit ? limit : limit-1);
				
				limit = getSettingsValue("torrent/maxconnections_loc").toInt();
				m_handle.set_max_connections(limit ? limit : limit-1);
			}
			
			if(localFile)
			{
				m_bHasHashCheck = true;
				
				createDefaultPriorityList();
				m_worker->doWork();
				storeTorrent(source);
				
				if(!m_bAuto)
					RssFetcher::performManualCheck(name());
			}
		}
		else
		{
			downloadTorrent(source);
		}
	}
	catch(const libtorrent::invalid_encoding&)
	{
		throw RuntimeException(tr("The torrent file is invalid."));
	}
	catch(const std::exception& e)
	{
		throw RuntimeException(e.what());
	}
}

void TorrentDownload::downloadTorrent(QString source)
{
	qDebug() << "downloadTorrent()";

	QUuid webseed = getSettingsValue("torrent/proxy_webseed").toString();
	Proxy pr = Proxy::getProxy(webseed);
	
	m_pFileDownload = new QNetworkAccessManager(this);
	m_pFileDownloadTemp = new QTemporaryFile(m_pFileDownload);

	if (pr.nType != Proxy::ProxyNone)
		m_pFileDownload->setProxy(pr);
	
	if(!m_pFileDownloadTemp->open())
	{
		delete m_pFileDownload;
		m_pFileDownload = 0;
		m_strError = tr("Cannot create a temporary file");
		setState(Failed);
		return;
	}
	connect(m_pFileDownload, SIGNAL(finished(QNetworkReply*)), this, SLOT(torrentFileDone(QNetworkReply*)), Qt::QueuedConnection);
	m_pReply = m_pFileDownload->get(QNetworkRequest(source));
	connect(m_pReply, SIGNAL(readyRead()), this, SLOT(torrentFileReadyRead()));
}

bool TorrentDownload::storeTorrent(QString orig)
{
	QString str = storedTorrentName();
	QDir dir = QDir::home();
	
	dir.mkpath(TORRENT_FILE_STORAGE);
	if(!dir.cd(TORRENT_FILE_STORAGE))
		return false;
	
	str = dir.absoluteFilePath(str);
	
	if(str != orig)
		return QFile::copy(orig, str);
	else
		return true;
}

bool TorrentDownload::storeTorrent()
{
	QString str = storedTorrentName();
	QDir dir = QDir::home();

	dir.mkpath(TORRENT_FILE_STORAGE);
	if(!dir.cd(TORRENT_FILE_STORAGE))
	{
		qDebug() << "Unable to cd into" << TORRENT_FILE_STORAGE;
		return false;
	}

	str = dir.absoluteFilePath(str);

	boost::shared_ptr<const libtorrent::torrent_info> info = m_handle.torrent_file();

	if (info)
	{
		boost::shared_array<char> md = info->metadata();
		int mdlen = info->metadata_size();

		QFile file(str);
		if(!file.open(QIODevice::ReadWrite))
		{
			qDebug() << "Unable to open" << str << "for writing";
			return false;
		}

		file.write("d4:info");
		file.write(md.get(), mdlen);
		file.write("e");
		file.close();
		return true;
	}
	else
		return false;
}

QString TorrentDownload::storedTorrentName() const
{
	if(!m_info)
		return QString();
	
	QString hash;
	
	const libtorrent::big_number& bn = m_info->info_hash();
	hash = QByteArray((char*) bn.begin(), 20).toHex();
	
	return QString("%1 - %2.torrent").arg(name()).arg(hash);
}

void TorrentDownload::torrentFileReadyRead()
{
	if (!m_pReply->rawHeader("Location").isEmpty())
		return;
	m_pFileDownloadTemp->write(m_pReply->readAll());
}

void TorrentDownload::torrentFileDone(QNetworkReply* reply)
{
	qDebug() << "TorrentDownload::torrentFileDone";
	reply->deleteLater();
	m_pReply = 0;

	if(reply->error() != QNetworkReply::NoError)
	{
		m_strError = tr("Failed to download the .torrent file");
		setState(Failed);
	}
	else
	{
		// working around a Qt bug here, rawHeader() is a necessity!
		QUrl redir = QUrl::fromEncoded(reply->rawHeader("Location"));
		qDebug() << "Redirect URL:" << redir;
		if (!redir.isEmpty())
		{
			QUrl newUrl = reply->url().resolved(redir);
			qDebug() << "Redirected to another URL:" << newUrl;
			QNetworkRequest nr(newUrl);
			nr.setRawHeader("Referer", reply->url().toEncoded());
			m_pReply = m_pFileDownload->get(nr);
			connect(m_pReply, SIGNAL(readyRead()), this, SLOT(torrentFileReadyRead()));
			return;
		}
		else
		{
			try
			{
				m_pFileDownloadTemp->flush();
				qDebug() << "Loading" << m_pFileDownloadTemp->fileName();
				init(m_pFileDownloadTemp->fileName(), m_strTarget);

				foreach (const QString& url, m_urlSeeds)
					m_handle.add_http_seed(url.toStdString());
				m_urlSeeds.clear();
			}
			catch(const RuntimeException& e)
			{
				qDebug() << "Failed to load torrent:" << e.what();
				m_strError = e.what();
				setState(Failed);
			}
		}
	}
	
	m_pFileDownload->deleteLater();
	m_pFileDownload = 0;
	m_pFileDownloadTemp = 0;
}

void TorrentDownload::setObject(QString target)
{
	if(m_handle.is_valid() && target != m_strTarget)
	{
		QByteArray path = target.toUtf8();
		std::string newplace = path.constData();
		try
		{
			m_handle.move_storage(newplace);
			m_strTarget = target;
		}
		catch(...)
		{
			throw RuntimeException(tr("Cannot change storage!"));
		}
	}
}

QString TorrentDownload::object() const
{
	return m_strTarget;
}

void TorrentDownload::changeActive(bool nowActive)
{
//	bool bEnableRecheck = false;
	
	if(m_handle.is_valid())
	{
		if(nowActive)
		{
			m_handle.resume();
			QTimer::singleShot(10000, this, SLOT(forceReannounce()));
		}
		else
		{
			//m_nPrevDownload = totalDownload();
			//m_nPrevUpload = totalUpload();
//			bEnableRecheck = true;
			m_handle.pause();
		}
	}
	/*else if(m_pFileDownload == 0)
	{
		qDebug() << "changeActive() and the handle is not valid";
		//setState(Failed);
	}*/
}

void TorrentDownload::setSpeedLimits(int down, int up)
{
	if(m_handle.is_valid())
	{
		if(!down)
			down--;
		if(!up)
			up--;
		
		m_handle.set_upload_limit(up);
		m_handle.set_download_limit(down);
	}
}

qulonglong TorrentDownload::done() const
{
	if(m_handle.is_valid())
		return qMax<qint64>(0, m_status.total_wanted_done);
	else
		return 0;
}

qulonglong TorrentDownload::total() const
{
	if(m_handle.is_valid())
		return m_status.total_wanted;
	else
		return 0;
}

void TorrentDownload::speeds(int& down, int& up) const
{
	if(m_handle.is_valid())
	{
		libtorrent::torrent_status status = m_handle.status();
		down = status.download_payload_rate;
		up = status.upload_payload_rate;
	}
	else
		down = up = 0;
}

QByteArray TorrentDownload::bencode_simple(libtorrent::entry& e)
{
	std::vector<char> buffer;
	libtorrent::bencode(std::back_inserter(buffer), e);
	//QVector<char> buf2 = QVector<char>::fromStdVector(buffer);
	return QByteArray(buffer.data(), buffer.size());
}

QString TorrentDownload::bencode(libtorrent::entry& e)
{
	return bencode_simple(e).toBase64();
}

libtorrent::entry TorrentDownload::bdecode_simple(QByteArray array)
{
	if(array.isEmpty())
		return libtorrent::entry();
	else
		return libtorrent::bdecode(array.constData(), array.constData() + array.size());
}

void TorrentDownload::lazy_bdecode_simple(libtorrent::lazy_entry& e, QByteArray array)
{
	libtorrent::error_code ec;
	if(!array.isEmpty())
		libtorrent::lazy_bdecode(array.constData(), array.constData() + array.size(), e, ec);
}

libtorrent::entry TorrentDownload::bdecode(QString d)
{
	if(d.isEmpty())
		return libtorrent::entry();
	else
	{
		QByteArray array;
		array = QByteArray::fromBase64(d.toUtf8());
		return libtorrent::bdecode(array.constData(), array.constData() + array.size());
	}
}

void TorrentDownload::load(const QDomNode& map)
{
	Transfer::load(map);
	
	try
	{
		QByteArray torrent_resume;
		QString str;
		QDir dir = QDir::home();
	
		dir.cd(TORRENT_FILE_STORAGE);
		
		str = getXMLProperty(map, "seedlimitratio");
		if(!str.isEmpty())
			m_seedLimitRatio = str.toDouble();
		else
			m_seedLimitRatio = getSettingsValue("torrent/maxratio").toDouble();
		
		str = getXMLProperty(map, "seedlimitupload");
		m_seedLimitUpload = str.toInt();

		m_bSuperSeeding = getXMLProperty(map, "superseeding").toInt() != 0;
		
		m_strTarget = str = getXMLProperty(map, "target");
		
		QString sfile = dir.absoluteFilePath( getXMLProperty(map, "torrent_file") );
		
		if(!QFile(sfile).open(QIODevice::ReadOnly))
		{
			m_strError = tr("Unable to open the file!");
			setState(Failed);
			return;
		}
		
		boost::shared_ptr<libtorrent::torrent_info> ti(new libtorrent::torrent_info(sfile.toStdString()));
		m_info = ti;
		
		torrent_resume = QByteArray::fromBase64(getXMLProperty(map, "torrent_resume").toUtf8());
		
		std::cout << "Loaded " << getXMLProperty(map, "torrent_resume").size() << " bytes of resume data\n";
		
		libtorrent::add_torrent_params params;
		std::vector<char> torrent_resume2 = std::vector<char>(torrent_resume.data(), torrent_resume.data()+torrent_resume.size());
		
		//params.storage_mode = (libtorrent::storage_mode_t) getSettingsValue("torrent/allocation").toInt();
		params.storage_mode = libtorrent::storage_mode_sparse; // don't force full allocation upon load
		params.ti = ti;
		
		QByteArray path = str.toUtf8();
		params.save_path = path.constData();
		if(!torrent_resume2.empty())
			params.resume_data = torrent_resume2;
		params.paused = true;
		params.auto_managed = false;
		
		m_handle = m_session->add_torrent(params);
		
		m_handle.set_max_uploads(getSettingsValue("torrent/maxuploads").toInt());
		m_handle.set_max_connections(getSettingsValue("torrent/maxconnections").toInt());
		
		//m_nPrevDownload = getXMLProperty(map, "downloaded").toLongLong();
		//m_nPrevUpload = getXMLProperty(map, "uploaded").toLongLong();
		
		str = getXMLProperty(map, "priorities");
		
		if(str.isEmpty())
			createDefaultPriorityList();
		else
		{
			QStringList priorities = str.split('|');
			int numFiles = m_info->num_files();
			
			if(numFiles != priorities.size())
				createDefaultPriorityList();
			else
			{
				m_vecPriorities.resize(numFiles);
				
				for(int i=0;i<numFiles;i++)
					m_vecPriorities[i] = priorities[i].toInt();
			}
			
			m_handle.prioritize_files(m_vecPriorities);
		}
		
		std::vector<libtorrent::announce_entry> trackers;
		QDomElement n = map.firstChildElement("trackers");
		if(!n.isNull())
		{
			QDomElement tracker = n.firstChildElement("tracker");
			while(!tracker.isNull())
			{
				QByteArray url = tracker.firstChild().toText().data().toUtf8();
				trackers.push_back(libtorrent::announce_entry(url.data()));
				tracker = tracker.nextSiblingElement("tracker");
			}
			m_handle.replace_trackers(trackers);
		}
		
		std::set<std::string> cur_seeds = m_handle.url_seeds();
		std::set<std::string> now_seeds;
		
		n = map.firstChildElement("url_seeds");
		if(!n.isNull())
		{
			QDomElement seed = n.firstChildElement("url");
			while(!seed.isNull())
			{
				QByteArray url = seed.firstChild().toText().data().toUtf8();
				now_seeds.insert(url.constData());
				seed = seed.nextSiblingElement("url");
			}
			
			std::set<std::string> diff;
			
			std::set_difference(cur_seeds.begin(), cur_seeds.end(), now_seeds.begin(), now_seeds.end(), std::inserter(diff, diff.begin()));
			for(std::set<std::string>::iterator it=diff.begin(); it != diff.end(); it++)
				m_handle.remove_url_seed(*it);
			
			diff.clear();
			
			std::set_difference(now_seeds.begin(), now_seeds.end(), cur_seeds.begin(), cur_seeds.end(), std::inserter(diff, diff.begin()));
			for(std::set<std::string>::iterator it=diff.begin(); it != diff.end(); it++)
				m_handle.add_url_seed(*it);
		}
		
		if(isActive())
			m_handle.resume();
		
		m_worker->doWork();
	}
	catch(const std::exception& e)
	{
		m_strError = e.what();
		setState(Failed);
	}
}

void TorrentDownload::addUrlSeed(QString str)
{
	if (!m_handle.is_valid())
		m_handle.add_url_seed(str.toStdString());
	else
		m_urlSeeds << str;
}

void TorrentDownload::save(QDomDocument& doc, QDomNode& map) const
{
	Transfer::save(doc, map);
	
	if(m_info != 0)
	{
		setXMLProperty(doc, map, "torrent_file", storedTorrentName());
		
		if(m_handle.is_valid() && m_status.state != libtorrent::torrent_status::downloading_metadata)
		{
			m_mutexAlerts.lock();
			m_handle.save_resume_data();
			
			int i;
			for(i = 0; i < 3;)
			{
				libtorrent::alert* aaa;
				std::unique_ptr<libtorrent::alert> a = TorrentDownload::m_session->pop_alert();
	
				if((aaa = a.get()) == 0)
				{
					Sleeper::msleep(250);
					i++;
					continue;
				}
				
				if(libtorrent::save_resume_data_alert* alert = dynamic_cast<libtorrent::save_resume_data_alert*>(aaa))
				{
					setXMLProperty(doc, map, "torrent_resume", bencode(*alert->resume_data));
					break;
				}
				else if(dynamic_cast<libtorrent::save_resume_data_failed_alert*>(aaa))
				{
					std::cout << "Save data failed\n";
					break;
				}
				else
					m_worker->processAlert(aaa);
			}
			m_mutexAlerts.unlock();

			if (i == 3)
				std::cout << "Torrent state did not get saved!\n";
		}
	}
	
	setXMLProperty(doc, map, "target", object());
	setXMLProperty(doc, map, "downloaded", QString::number( totalDownload() ));
	setXMLProperty(doc, map, "uploaded", QString::number( totalUpload() ));
	
	setXMLProperty(doc, map, "seedlimitratio", QString::number(m_seedLimitRatio));
	setXMLProperty(doc, map, "seedlimitupload", QString::number(m_seedLimitUpload));
	setXMLProperty(doc, map, "superseeding", QString::number(m_bSuperSeeding));
	
	QString prio;
	
	for(size_t i=0;i<m_vecPriorities.size();i++)
	{
		if(i > 0)
			prio += '|';
		prio += QString::number(m_vecPriorities[i]);
	}
	setXMLProperty(doc, map, "priorities", prio);
	
	if(m_handle.is_valid())
	{
		QDomElement sub = doc.createElement("trackers");
		std::vector<libtorrent::announce_entry> trackers = m_handle.trackers();
		for(size_t i=0;i<trackers.size();i++)
		{
			setXMLProperty(doc, sub, "tracker", QString::fromUtf8(trackers[i].url.c_str()));
		}
		
		sub = doc.createElement("url_seeds");
		std::set<std::string> seeds = m_handle.url_seeds();
		for(std::set<std::string>::iterator it=seeds.begin(); it != seeds.end(); it++)
		{
			setXMLProperty(doc, sub, "url", QString::fromUtf8(it->c_str()));
		}
	}
}

QString TorrentDownload::message() const
{
	QString state;
	
	if(this->state() == Failed)
		return m_strError;
	else if(m_pFileDownload != 0)
		return tr("Downloading the .torrent file");
	
	if(!m_status.paused)
	{
		switch(m_status.state)
		{
		case libtorrent::torrent_status::queued_for_checking:
			state = tr("Queued for checking");
			break;
		case libtorrent::torrent_status::checking_files:
			state = tr("Checking files: %1%").arg(m_status.progress*100.f);
			break;
		/*case libtorrent::torrent_status::connecting_to_tracker:
			state = tr("Connecting to the tracker");
			state += " | ";
		*/
		case libtorrent::torrent_status::seeding:
		case libtorrent::torrent_status::downloading:
		case libtorrent::torrent_status::finished:
			state += tr("Seeders: %1 (%2) | Leechers: %3 (%4)");
			
			if(m_status.state == libtorrent::torrent_status::downloading)
				state = state.arg(m_status.num_seeds);
			else
				state = state.arg(QString());
			
			if(m_status.num_complete >= 0)
				state = state.arg(m_status.num_complete);
			else
				state = state.arg('?');
			state = state.arg(m_status.num_peers - m_status.num_seeds);
			
			if(m_status.num_incomplete >= 0)
				state = state.arg(m_status.num_incomplete);
			else
				state = state.arg('?');
			break;
		case libtorrent::torrent_status::allocating:
			state = tr("Allocating: %1%").arg(m_status.progress*100.f);
			break;
		case libtorrent::torrent_status::downloading_metadata:
			state = tr("Downloading metadata");
			break;
		case libtorrent::torrent_status::checking_resume_data:
			state = tr("Checking resume data: %1%").arg(m_status.progress*100.f);
			break;
		}
	}
	else
	{
		if(m_status.num_complete >= 0 || m_status.num_incomplete >= 0)
		{
		   state = tr("Seeders: %1 | Leechers: %2");
			if (m_status.num_complete >= 0)
				state = state.arg(m_status.num_complete);
			else
				state = state.arg("?");
			
			if (m_status.num_incomplete >= 0)
				state = state.arg(m_status.num_incomplete);
			else
				state = state.arg("?");
		}
	}
	
	return state;
}

#ifdef WITH_WEBINTERFACE
void TorrentDownload::process(QString method, QMap<QString,QString> args, WriteBack* wb)
{
	qDebug() << "TorrentDownload::process" << method;

	if (m_handle.is_valid() && m_info)
	{
		const int WIDTH = 800;
		if (method == "progress")
		{
			libtorrent::bitfield pieces = m_handle.status().pieces;
			QImage img(WIDTH, 1, QImage::Format_RGB32);

			if(pieces.empty() && m_info->total_size() == m_status.total_done)
			{
				pieces.resize(m_info->num_pieces());
				pieces.set_all();
			}
			if (!pieces.empty())
				TorrentProgressWidget::generate(pieces, WIDTH, reinterpret_cast<quint32*>(img.bits()));
			else
				img.fill(0xffffffff);

			QBuffer bbuf;

			img.save(&bbuf, "PNG");
			wb->setContentType("image/png");

			wb->write(bbuf.buffer().data(), bbuf.size());
			wb->send();
		}
		else if (method == "availability")
		{
			std::vector<int> avail;
			QBuffer bbuf;

			m_handle.piece_availability(avail);

			QImage img(WIDTH, 1, QImage::Format_RGB32);
			TorrentProgressWidget::generate(avail, WIDTH, reinterpret_cast<quint32*>(img.bits()));

			img.save(&bbuf, "PNG");
			wb->setContentType("image/png");

			wb->write(bbuf.buffer().data(), bbuf.size());
			wb->send();
		}
		else
			wb->writeFail("Unknown request");
	}
}

const char* TorrentDownload::detailsScript() const
{
	return "/scripts/transfers/TorrentDownload.js";
}

QVariantMap TorrentDownload::properties() const
{
	QVariantMap rv;
	
	if (!m_info)
		return rv;
	
	qint64 d, u;
	QString ratio;

	d = totalDownload();
	u = totalUpload();

	rv["totalDownload"] = formatSize(d);
	rv["totalUpload"] = formatSize(u);

	QString comment = m_info->comment().c_str();
	comment.replace('\n', "<br>");
	rv["comment"] = comment;

	rv["magnetLink"] = remoteURI();

	if(!d)
		ratio = QString::fromUtf8("∞");
	else
		ratio = QString::number(double(u)/double(d));
	rv["ratio"] = ratio;

	QVariantList files;
	std::vector<libtorrent::size_type> progresses;

	m_handle.file_progress(progresses);

	// num_files(), file_at()
	for (int i = 0; i < m_info->num_files(); i++)
	{
		QVariantMap map;
		libtorrent::file_entry fe;

		fe = m_info->file_at(i);

		map["name"] = QString::fromStdString(fe.path);
		map["size"] = qint64(fe.size);
		map["done"] = qint64(progresses[i]);
		map["priority"] = m_vecPriorities[i];
		files << map;
	}
	rv["files"] = files;

	return rv;
}
#endif

TorrentWorker::TorrentWorker()
{
	m_timer.start(1000);
	connect(&m_timer, SIGNAL(timeout()), this, SLOT(doWork()));
}

void TorrentWorker::addObject(TorrentDownload* d)
{
	QMutexLocker l(&m_mutex);
	m_objects << d;
}

void TorrentWorker::removeObject(TorrentDownload* d)
{
	QMutexLocker l(&m_mutex);
	m_objects.removeAll(d);
}

TorrentDownload* TorrentWorker::getByHandle(libtorrent::torrent_handle handle) const
{
	foreach(TorrentDownload* d, m_objects)
	{
		if(d->m_handle == handle)
			return d;
	}
	return 0;
}

void TorrentWorker::processAlert(libtorrent::alert* aaa)
{
#define IS_ALERT(type) libtorrent::type* alert = dynamic_cast<libtorrent::type*>(aaa)
#define IS_ALERT_S(type) dynamic_cast<libtorrent::type*>(aaa) != 0

	if(IS_ALERT(torrent_alert))
	{
		TorrentDownload* d = getByHandle(alert->handle);
		std::string smsg = aaa->message();
		QString errmsg = QString::fromUtf8(smsg.c_str());
			
		if(!d)
			return;
			
		if(IS_ALERT_S(file_error_alert))
		{
			d->setState(Transfer::Failed);
			d->m_strError = errmsg;
			d->enterLogMessage(tr("File error: %1").arg(errmsg));
		}
		else if(IS_ALERT_S(tracker_announce_alert))
		{
			d->enterLogMessage(tr("Tracker announce: %1").arg(errmsg));
		}
		else if(IS_ALERT(tracker_error_alert))
		{
			QString desc = tr("Tracker failure: %1, %2 times in a row ")
					.arg(errmsg)
					.arg(alert->times_in_row);
				
			if(alert->status_code != 0)
				desc += tr("(error %1)").arg(alert->status_code);
			else
				desc += tr("(timeout)");
			d->enterLogMessage(desc);
		}
		else if(IS_ALERT_S(tracker_warning_alert))
		{
			d->enterLogMessage(tr("Tracker warning: %1").arg(errmsg));
		}
		else if(IS_ALERT_S(fastresume_rejected_alert))
		{
			d->enterLogMessage(tr("The fast-resume data have been rejected: %1").arg(errmsg));
		}
		else if(IS_ALERT_S(metadata_failed_alert))
		{
			d->enterLogMessage(tr("Failed to retrieve the metadata"));
		}
		else if(IS_ALERT_S(metadata_received_alert))
		{
			d->enterLogMessage(tr("Successfully retrieved the metadata"));
			d->storeTorrent();

			if (!d->m_info)
				d->m_info = d->m_handle.torrent_file();

			d->createDefaultPriorityList();
		}
	}
	else
	{
#ifdef LIBTORRENT_0_15
		if (!IS_ALERT_S(dht_announce_alert) && !IS_ALERT_S(dht_get_peers_alert))
		{
			Logger::global()->enterLogMessage("BitTorrent", aaa->message().c_str());
		}
#else
		Logger::global()->enterLogMessage("BitTorrent", aaa->message().c_str());
#endif

#undef IS_ALERT
#undef IS_ALERT_S

	}
}

void TorrentWorker::doWork()
{
	QMutexLocker l(&m_mutex);
	
	foreach(TorrentDownload* d, m_objects)
	{
		if(!d->m_handle.is_valid())
			continue;
		
		d->m_status = d->m_handle.status();
		
		if(!d->m_info)
		{
            if(!d->m_status.has_metadata)
				continue;
			d->m_info = d->m_handle.torrent_file();
		}
	}
	
	foreach(TorrentDownload* d, m_objects)
	{
		if(!d->m_handle.is_valid() || !d->m_info)
			continue;
		
		if(d->m_bHasHashCheck && d->m_status.state != libtorrent::torrent_status::checking_files && d->m_status.state != libtorrent::torrent_status::queued_for_checking)
		{
			d->m_bHasHashCheck = false;
			//d->m_nPrevDownload = d->done();
		}
		
		if(d->isActive())
		{
			if(d->mode() == Transfer::Download)
			{
				if(d->m_status.state == libtorrent::torrent_status::finished ||
				  d->m_status.state == libtorrent::torrent_status::seeding || d->m_status.is_seeding)
				{
					d->setMode(Transfer::Upload);
					d->enterLogMessage(tr("The torrent has been downloaded"));
				}
				else if(d->m_status.total_wanted == d->m_status.total_wanted_done)
				{
					d->enterLogMessage(tr("Requested parts of the torrent have been downloaded"));
					d->setMode(Transfer::Upload);
				}
				d->m_handle.super_seeding(d->m_bSuperSeeding);
			}
			if(d->mode() == Transfer::Upload)
			{
				if(d->m_status.total_wanted_done < d->m_status.total_wanted)
					d->setMode(Transfer::Download);
				else if(d->state() != Transfer::ForcedActive)
				{
					if(double(d->totalUpload()) / d->totalDownload() >= d->m_seedLimitRatio
					  || (d->totalUpload() >= d->m_seedLimitUpload*1024*1024 && d->m_seedLimitUpload))
					{
						d->setState(Transfer::Completed);
						d->setMode(Transfer::Download);
					}
				}
				if (d->m_status.super_seeding != d->m_bSuperSeeding)
					d->m_handle.super_seeding(d->m_bSuperSeeding);
			}
			
			int down, up, sdown, sup;
			
			down = d->m_handle.download_limit();
			up = d->m_handle.upload_limit();
			
			d->internalSpeedLimits(sdown, sup);
			
			if(!sdown) sdown--;
			if(!sup) sup--;
			
			if(down != sdown || up != sup)
				d->setSpeedLimits(sdown, sup);
		}
	}
	
	QMutexLocker ll(&TorrentDownload::m_mutexAlerts);
	while(true)
	{
		libtorrent::alert* aaa;
		std::unique_ptr<libtorrent::alert> a = TorrentDownload::m_session->pop_alert();
		
		if((aaa = a.get()) == 0)
			break;
		
		processAlert(aaa);
	}
	
	libtorrent::session_status st = TorrentDownload::m_session->status();
	if(TorrentDownload::m_bDHT && TorrentDownload::m_labelDHTStats)
	{
		QString text = tr("<b>DHT:</b> %1 nodes (%2 globally)").arg(st.dht_nodes).arg(st.dht_global_nodes);
		TorrentDownload::m_labelDHTStats->setText(text);
	}
}

void TorrentDownload::forceReannounce()
{
	if(!m_handle.is_valid())
		return;
	
	if(isActive())
	{
		qDebug() << "TorrentDownload::forceReannounce(): announce";
		m_handle.force_reannounce();
	}
	else
	{
		qDebug() << "TorrentDownload::forceReannounce(): scrape";
		m_handle.scrape_tracker();
	}
}

void TorrentDownload::forceRecheck()
{
	if(isActive() || !m_handle.is_valid())
		return;
	
	m_bHasHashCheck = false;
	
	m_handle.force_recheck();
}

void TorrentDownload::fillContextMenu(QMenu& menu)
{
	QAction* a;
	
	a = menu.addAction(tr("Force announce"));
	connect(a, SIGNAL(triggered()), this, SLOT(forceReannounce()));
	a = menu.addAction(QIcon(":/menu/reload.png"), tr("Recheck files"));
	a->setDisabled(isActive());
	connect(a, SIGNAL(triggered()), this, SLOT(forceRecheck()));
}

QObject* TorrentDownload::createDetailsWidget(QWidget* widget)
{
	return new TorrentDetails(widget, this);
}

WidgetHostChild* TorrentDownload::createOptionsWidget(QWidget* w)
{
	return new TorrentOptsWidget(w, this);
}

QString TorrentDownload::remoteURI() const
{
	if (!m_handle.is_valid())
		return QString();
	else
		return QString::fromStdString(libtorrent::make_magnet_uri(m_handle));
}

#ifdef WITH_WEBINTERFACE
QVariant TorrentDownload::setFilePriorities(QList<QVariant>& args)
{
	QString uuid = args[0].toString();
	Queue* q = 0;
	Transfer* t = 0;
	TorrentDownload* td = 0;
	QVariantMap vmap;

	XmlRpcService::findTransfer(uuid, &q, &t);
	if (!t)
		throw XmlRpcService::XmlRpcError(102, "Invalid transfer UUID");

	if (! (td = dynamic_cast<TorrentDownload*>(t)) || !td->m_handle.is_valid())
	{
		q->unlock();
		g_queuesLock.unlock();
		return QVariant();
	}

	try
	{
		QVariantMap map = args[1].toMap();

		for (QVariantMap::iterator it = map.begin(); it != map.end(); it++)
		{
			quint32 i = it.key().toUInt();
			if (i >= 0 && i < td->m_vecPriorities.size())
				td->m_vecPriorities[i] = it.value().toInt();
			else
				; // TODO throw exception
		}

		td->m_handle.prioritize_files(td->m_vecPriorities);
	}
	catch (...)
	{
		q->unlock();
		g_queuesLock.unlock();
		throw;
	}

	q->unlock();
	g_queuesLock.unlock();

	return QVariant();
}
#endif

void TorrentWorker::setDetailsObject(TorrentDetails* d)
{
	connect(&m_timer, SIGNAL(timeout()), d, SLOT(refresh()));
}
