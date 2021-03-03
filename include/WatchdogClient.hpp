#ifndef WATCHDOG_CLIENT_HPP
#define WATCHDOG_CLIENT_HPP

#include "DataMailbox.hpp"


class WatchdogClient
{
private:
	typedef enum
	{
		UNREGISTERED,
		STOPPED,
		RUNNING,
		SYNC,
		TERMINATING
	} enuStatus;

public:
	WatchdogClient(const std::string& unitName, ILogger* pLogger = NulLogger::getInstance());
	~WatchdogClient();

	void Register(const std::string& serverName, const SlotSettings& settings, enuActionOnFailure onFailure);
	void Unregister();
	void Start();
	void Stop();
	bool Kick();
	void UpdateSettings(const SlotSettings& settings);
	void Sync();
	void Terminate();

private:

	void waitForServerReply(WatchdogMessage::MessageClass expectedMessageClass,
		enuReceiveOptions timedFlag = enuReceiveOptions::NORMAL,
		long timeout_ms = 0);

	void sendSignal(WatchdogMessage::MessageClass messageClass, enuReceiveOptions sendOptions = enuReceiveOptions::NORMAL);

	void setSettings(const SlotSettings& settings);

	WatchdogMessage receiveAndCheckInputMessageFrom(const std::string& sourceName, enuReceiveOptions timedFlag = enuReceiveOptions::NORMAL, long timeout_ms = 0);

	unsigned int m_PID;

	SlotSettings m_settings;

	std::string m_unitName;
	std::string m_serverName;

	DataMailbox m_mailbox;

	ILogger* m_pLogger;

	enuStatus m_status;
	enuActionOnFailure m_onFailure;

	MailboxReference m_server;

};

#endif
