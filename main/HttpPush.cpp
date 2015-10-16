#include "stdafx.h"
#include "HttpPush.h"
#include "Helper.h"
#include "../httpclient/HTTPClient.h"
#include "Logger.h"
#include "../hardware/hardwaretypes.h"
#include "RFXtrx.h"
#include "SQLHelper.h"
#include <boost/date_time/c_local_time_adjustor.hpp>
#include "../webserver/Base64.h"
#include "../main/localtime_r.h"
#include "../main/WebServer.h"
#include "../webserver/cWebem.h"
#include "../main/mainworker.h"
#include "../json/json.h"

#ifndef WIN32
	#include <unistd.h> //gethostbyname
#endif

typedef struct _STR_TABLE_ID1_ID2 {
	unsigned long    id1;
	unsigned long    id2;
	const char   *str1;
} STR_TABLE_ID1_ID2;

static boost::posix_time::time_duration get_utc_offset() {
	using namespace boost::posix_time;

	// boost::date_time::c_local_adjustor uses the C-API to adjust a
	// moment given in utc to the same moment in the local time zone.
	typedef boost::date_time::c_local_adjustor<ptime> local_adj;

	const ptime utc_now = second_clock::universal_time();
	const ptime now = local_adj::utc_to_local(utc_now);

	return now - utc_now;
}

CHttpPush::CHttpPush()
{
	m_bLinkActive = false;
}

void CHttpPush::Start()
{
	UpdateActive();
	m_sConnection = m_mainworker.sOnDeviceReceived.connect(boost::bind(&CHttpPush::OnDeviceReceived, this, _1, _2, _3, _4));
}

void CHttpPush::Stop()
{
	if (m_sConnection.connected())
		m_sConnection.disconnect();
}


void CHttpPush::UpdateActive()
{
	int fActive;
	m_sql.GetPreferencesVar("HttpActive", fActive);
	m_bLinkActive = (fActive == 1);
}

void CHttpPush::OnDeviceReceived(const int m_HwdID, const unsigned long long DeviceRowIdx, const std::string &DeviceName, const unsigned char *pRXCommand)
{
	m_DeviceRowIdx = DeviceRowIdx;
	if (m_bLinkActive)
	{
		DoHttpPush();
	}
}

void CHttpPush::DoHttpPush()
{			
	std::string httpUrl = "";
	std::string httpData = "";
	int httpMethodInt = 0;
	int httpAuthInt = 0;
	std::string httpAuth = "";
	std::string httpAuthBasicLogin = "";
	std::string httpAuthBasicPassword = "";
	m_sql.GetPreferencesVar("HttpMethod", httpMethodInt);
	m_sql.GetPreferencesVar("HttpAuth", httpAuthInt);
	m_sql.GetPreferencesVar("HttpAuthBasicLogin", httpAuthBasicLogin);
	m_sql.GetPreferencesVar("HttpAuthBasicPassword", httpAuthBasicPassword);

	int httpDebugActiveInt;
	bool httpDebugActive = false;
	m_sql.GetPreferencesVar("HttpDebug", httpDebugActiveInt);
	if (httpDebugActiveInt == 1) {
		httpDebugActive = true;
	}
	std::vector<std::vector<std::string> > result;
	result=m_sql.safe_query(
		"SELECT A.DeviceID, A.DelimitedValue, B.ID, B.Type, B.SubType, B.nValue, B.sValue, A.TargetType, A.TargetVariable, A.TargetDeviceID, A.TargetProperty, A.IncludeUnit, B.SwitchType, strftime('%%s', B.LastUpdate), B.Name FROM HttpLink as A, DeviceStatus as B "
		"WHERE (A.DeviceID == '%llu' AND A.Enabled = '1' AND A.DeviceID==B.ID)",
		m_DeviceRowIdx);
	if (result.size()>0)
	{
		std::string sendValue;
		std::vector<std::vector<std::string> >::const_iterator itt;
		for (itt=result.begin(); itt!=result.end(); ++itt)
		{
			m_sql.GetPreferencesVar("HttpUrl", httpUrl);
			m_sql.GetPreferencesVar("HttpData", httpData);
			if (httpUrl == "")
				return;

			std::vector<std::string> sd=*itt;
			unsigned int deviceId = atoi(sd[0].c_str());
			std::string ldelpos = sd[1].c_str();
			int delpos = atoi(sd[1].c_str());
			int dType = atoi(sd[3].c_str());
			int dSubType = atoi(sd[4].c_str());
			std::string lType = sd[3].c_str();
			std::string lSubType = sd[4].c_str();
			int nValue = atoi(sd[5].c_str());
			std::string sValue = sd[6].c_str();
			int targetType = atoi(sd[7].c_str());
			std::string targetVariable = sd[8].c_str();
			int targetDeviceID = atoi(sd[9].c_str());
			std::string targetProperty = sd[10].c_str();
			int includeUnit = atoi(sd[11].c_str());
			int metertype = atoi(sd[12].c_str());
			int lastUpdate = atoi(sd[13].c_str());
			std::string lstatus="";
			std::string lunit = "";
			std::string ltargetVariable = sd[8].c_str();
			std::string ltargetDeviceId = sd[9].c_str();
			std::string lname = sd[14].c_str();
			sendValue = sValue;

			// Compute tz
			boost::posix_time::time_duration uoffset = get_utc_offset();
			unsigned long tzoffset = (int)((double)(uoffset.ticks() / 3600000000LL) * 3600);

#ifdef WIN32
			unsigned __int64 localTime = lastUpdate;
			unsigned __int64 localTimeUtc = lastUpdate - tzoffset;
#else
			unsigned long long int localTime = lastUpdate;
			unsigned long long int localTimeUtc = lastUpdate - tzoffset;
#endif

			char szLocalTime[16];
			sprintf(szLocalTime, "%llu", localTime);
			char szLocalTimeUtc[16];
			sprintf(szLocalTimeUtc, "%llu", localTimeUtc);
			char szLocalTimeMs[16];
			sprintf(szLocalTimeMs, "%llu", localTime*1000);
			char szLocalTimeUtcMs[16];
			sprintf(szLocalTimeUtcMs, "%llu", localTimeUtc * 1000);

			// RFC3339 time format
			time_t tmpT = localTimeUtc;
			struct tm* timeinfo = gmtime(&tmpT);

			char llastUpdate[255];
#if !defined WIN32
			snprintf(llastUpdate, sizeof(llastUpdate), "%04d-%02d-%02dT%02d:%02d:%02dZ",
				timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
#else
			sprintf_s(llastUpdate, sizeof(llastUpdate), "%04d-%02d-%02dT%02d:%02d:%02dZ",
				timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
#endif

			// Replace keywords
			/*
			%v : Value
			%t0 : Timestamp (epoc time localtime)
			%t1 : Timestamp (epoc ms localtime)
			%t2 : Timestamp (epoc time UTC)
			%t3 : Timestamp (epoc ms UTC)
			%t4 : Timestamp : "2015-01-29T21:50:44Z"
			%D : Target Device id
			%V : Target Variable
			%u : Unit
			%n : Name
			%T0 : Type
			%T1 : SubType
			%h : hostname
			*/

			lunit = getUnit(delpos, metertype);			
			lType = RFX_Type_Desc(dType,1);
			lSubType = RFX_Type_SubType_Desc(dType,dSubType);
			
			char hostname[256];
			gethostname(hostname, sizeof(hostname));

			std::vector<std::string> strarray;
			if (sendValue.find(";")!=std::string::npos)
			{
				StringSplit(sendValue, ";", strarray);
				if (int(strarray.size())>=delpos)
				{
					std::string rawsendValue = strarray[delpos-1].c_str();
					sendValue = ProcessSendValue(rawsendValue,delpos,nValue,false,metertype);
				}
			}
			else
			{
				sendValue = ProcessSendValue(sendValue,delpos,nValue,false,metertype);
			}
			ltargetDeviceId+="_";
			ltargetDeviceId+=ldelpos;

			replaceAll(httpUrl, "%v", sendValue);
			replaceAll(httpUrl, "%u", includeUnit ? lunit : "");
			replaceAll(httpUrl, "%D", ltargetDeviceId);
			replaceAll(httpUrl, "%V", ltargetVariable);
			replaceAll(httpUrl, "%t0", std::string(szLocalTime));
			replaceAll(httpUrl, "%t1", std::string(szLocalTimeMs));
			replaceAll(httpUrl, "%t2", std::string(szLocalTimeUtc));
			replaceAll(httpUrl, "%t3", std::string(szLocalTimeUtcMs));
			replaceAll(httpUrl, "%t4", std::string(llastUpdate));
			replaceAll(httpUrl, "%n", std::string(lname));
			replaceAll(httpUrl, "%T0", lType);
			replaceAll(httpUrl, "%T1", lSubType);
			replaceAll(httpUrl, "%h", std::string(hostname));

			replaceAll(httpData, "%v", sendValue);
			replaceAll(httpData, "%u", includeUnit ? lunit : "");
			replaceAll(httpData, "%D", ltargetDeviceId);
			replaceAll(httpData, "%V", ltargetVariable);
			replaceAll(httpData, "%t0", std::string(szLocalTime));
			replaceAll(httpData, "%t1", std::string(szLocalTimeMs));
			replaceAll(httpData, "%t2", std::string(szLocalTimeUtc));
			replaceAll(httpData, "%t3", std::string(szLocalTimeUtcMs));
			replaceAll(httpData, "%t4", std::string(llastUpdate));
			replaceAll(httpData, "%n", std::string(lname));
			replaceAll(httpData, "%T0", lType);
			replaceAll(httpData, "%T1", lSubType);
			replaceAll(httpData, "%h", std::string(hostname));

			if (sendValue != "") {
				std::string sResult;
				std::vector<std::string> ExtraHeaders;
				if (httpAuthInt == 1) {			// BASIC authentication
					std::stringstream sstr;
					sstr << httpAuthBasicLogin << ":" << httpAuthBasicPassword;
					std::string m_AccessToken = base64_encode((const unsigned char *)(sstr.str().c_str()), strlen(sstr.str().c_str()));
					ExtraHeaders.push_back("Authorization:Basic " + m_AccessToken);
				}
				sendValue = CURLEncode::URLEncode(sendValue);

				// data
				if (httpDebugActive) {
					_log.Log(LOG_NORM, "HttpLink: sending global variable %s with value: %s", targetVariable.c_str(), sendValue.c_str());
				}

				if (httpMethodInt == 0) {			// GET
					if (!HTTPClient::GET(httpUrl, ExtraHeaders, sResult))
					{
						_log.Log(LOG_ERROR, "Error sending data to http with GET!");
					}
				}
				else if (httpMethodInt == 1) {		// POST
					if (!HTTPClient::POST(httpUrl, httpData, ExtraHeaders, sResult))
					{
						_log.Log(LOG_ERROR, "Error sending data to http with POST!");
					}
				}
				else if(httpMethodInt == 2) {		// PUT
					if (!HTTPClient::PUT(httpUrl, httpData, ExtraHeaders, sResult))
					{
						_log.Log(LOG_ERROR, "Error sending data to http with PUT!");
					}
				}

				// debug
				if (httpDebugActive) {
					_log.Log(LOG_NORM, "HttpLink: response ", sResult.c_str());
				}
			}
		}
	}
}

//Webserver helpers
namespace http {
	namespace server {
		void CWebServer::Cmd_SaveHttpLinkConfig(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				//No admin user, and not allowed to be here
				return;
			}

			std::string url = m_pWebEm->FindValue("url");
			std::string method = m_pWebEm->FindValue("method");
			std::string data = m_pWebEm->FindValue("data");
			std::string linkactive = m_pWebEm->FindValue("linkactive");
			std::string debugenabled = m_pWebEm->FindValue("debugenabled");
			std::string auth = m_pWebEm->FindValue("auth");
			std::string authbasiclogin = m_pWebEm->FindValue("authbasiclogin");
			std::string authbasicpassword = m_pWebEm->FindValue("authbasicpassword");
			if (
				(url == "") ||
				(data == "") ||
				(method == "") ||
				(linkactive == "") ||
				(debugenabled == "")
				)
				return;
			int ilinkactive = atoi(linkactive.c_str());
			int idebugenabled = atoi(debugenabled.c_str());
			m_sql.UpdatePreferencesVar("HttpUrl", url.c_str());
			m_sql.UpdatePreferencesVar("HttpMethod", atoi(method.c_str()));
			m_sql.UpdatePreferencesVar("HttpData", data.c_str());
			m_sql.UpdatePreferencesVar("HttpActive", ilinkactive);
			m_sql.UpdatePreferencesVar("HttpDebug", idebugenabled);
			m_sql.UpdatePreferencesVar("HttpAuth", atoi(auth.c_str()));
			m_sql.UpdatePreferencesVar("HttpAuthBasicLogin", authbasiclogin.c_str());
			m_sql.UpdatePreferencesVar("HttpAuthBasicPassword", authbasicpassword.c_str());

			m_mainworker.m_httppush.UpdateActive();
			root["status"] = "OK";
			root["title"] = "SaveHttpLinkConfig";
		}

		void CWebServer::Cmd_GetHttpLinkConfig(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string sValue;
			int nValue;
			if (m_sql.GetPreferencesVar("HttpActive", nValue)) {
				root["HttpActive"] = nValue;
			}
			else {
				root["HttpActive"] = 0;
			}
			if (m_sql.GetPreferencesVar("HttpDebug", nValue)) {
				root["HttpDebug"] = nValue;
			}
			else {
				root["HttpDebug"] = 0;
			}
			if (m_sql.GetPreferencesVar("HttpUrl", sValue))
			{
				root["HttpUrl"] = sValue;
			}
			if (m_sql.GetPreferencesVar("HttpData", sValue))
			{
				root["HttpData"] = sValue;
			}
			if (m_sql.GetPreferencesVar("HttpMethod", nValue))
			{
				root["HttpMethod"] = nValue;
			}
			else {
				root["HttpMethod"] = 0;
			}
			if (m_sql.GetPreferencesVar("HttpAuth", nValue))
			{
				root["HttpAuth"] = nValue;
			}
			else {
				root["HttpAuth"] = 0;
			}
			if (m_sql.GetPreferencesVar("HttpAuthBasicLogin", sValue))
			{
				root["HttpAuthBasicLogin"] = sValue;
			}
			if (m_sql.GetPreferencesVar("HttpAuthBasicPassword", sValue))
			{
				root["HttpAuthBasicPassword"] = sValue;
			}
			root["status"] = "OK";
			root["title"] = "GetHttpLinkConfig";
		}

		void CWebServer::Cmd_GetHttpLinks(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT A.ID,A.DeviceID,A.Delimitedvalue,A.TargetType,A.TargetVariable,A.TargetDeviceID,A.TargetProperty,A.Enabled, B.Name, A.IncludeUnit FROM HttpLink as A, DeviceStatus as B WHERE (A.DeviceID==B.ID)");
			if (result.size() > 0)
			{
				std::vector<std::vector<std::string> >::const_iterator itt;
				int ii = 0;
				for (itt = result.begin(); itt != result.end(); ++itt)
				{
					std::vector<std::string> sd = *itt;
					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["DeviceID"] = sd[1];
					root["result"][ii]["Delimitedvalue"] = sd[2];
					root["result"][ii]["TargetType"] = sd[3];
					root["result"][ii]["TargetVariable"] = sd[4];
					root["result"][ii]["TargetDevice"] = sd[5];
					root["result"][ii]["TargetProperty"] = sd[6];
					root["result"][ii]["Enabled"] = sd[7];
					root["result"][ii]["Name"] = sd[8];
					root["result"][ii]["IncludeUnit"] = sd[9];
					ii++;
				}
			}
			root["status"] = "OK";
			root["title"] = "GetHttpLinks";
		}

		void CWebServer::Cmd_SaveHttpLink(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string idx = m_pWebEm->FindValue("idx");
			std::string deviceid = m_pWebEm->FindValue("deviceid");
			int deviceidi = atoi(deviceid.c_str());
			std::string valuetosend = m_pWebEm->FindValue("valuetosend");
			std::string targettype = m_pWebEm->FindValue("targettype");
			int targettypei = atoi(targettype.c_str());
			std::string targetvariable = m_pWebEm->FindValue("targetvariable");
			std::string targetdeviceid = m_pWebEm->FindValue("targetdeviceid");
			std::string targetproperty = m_pWebEm->FindValue("targetproperty");
			std::string linkactive = m_pWebEm->FindValue("linkactive");
			std::string includeunit = m_pWebEm->FindValue("includeunit");
			if ((targettypei == 0) && (targetvariable == ""))
				return;
			if ((targettypei == 1) && ((targetdeviceid == "") || (targetproperty == "")))
				return;
			if ((targettypei == 2) && (targetdeviceid == ""))
				return;
			if (idx == "0") {
				m_sql.safe_query(
					"INSERT INTO HttpLink (DeviceID,DelimitedValue,TargetType,TargetVariable,TargetDeviceID,TargetProperty,IncludeUnit,Enabled) VALUES ('%d','%d','%d','%q','%d','%q','%d','%d')",
					deviceidi,
					atoi(valuetosend.c_str()),
					targettypei,
					targetvariable.c_str(),
					atoi(targetdeviceid.c_str()),
					targetproperty.c_str(),
					atoi(includeunit.c_str()),
					atoi(linkactive.c_str())
					);
			}
			else {
				m_sql.safe_query(
					"UPDATE HttpLink SET DeviceID='%d', DelimitedValue=%d, TargetType=%d, TargetVariable='%q', TargetDeviceID=%d, TargetProperty='%q', IncludeUnit='%d', Enabled='%d' WHERE (ID == '%q')",
					deviceidi,
					atoi(valuetosend.c_str()),
					targettypei,
					targetvariable.c_str(),
					atoi(targetdeviceid.c_str()),
					targetproperty.c_str(),
					atoi(includeunit.c_str()),
					atoi(linkactive.c_str()),
					idx.c_str()
					);
			}
			root["status"] = "OK";
			root["title"] = "SaveHttpLink";
		}

		void CWebServer::Cmd_DeleteHttpLink(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				//No admin user, and not allowed to be here
				return;
			}

			std::string idx = m_pWebEm->FindValue("idx");
			if (idx == "")
				return;
			m_sql.safe_query("DELETE FROM HttpLink WHERE (ID=='%q')", idx.c_str());
			root["status"] = "OK";
			root["title"] = "DeleteHttpLink";
		}
	}
}
