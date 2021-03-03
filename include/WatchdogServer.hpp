#ifndef WATCHDOG_SERVER_HPP
#define WATCHDOG_SERVER_HPP

#include "DataMailbox.hpp"
#include "Timer.hpp"
#include "ProcessManager.hpp"

#include<list>

#include <mutex>

class WatchdogUnit
{
public:

	WatchdogUnit(const std::string& name, const SlotSettings& settings,
		unsigned int PID,
		enuActionOnFailure onFailure = enuActionOnFailure::RESET_ONLY,
		ILogger* pLogger = NulLogger::getInstance());

	~WatchdogUnit();

	friend bool operator==(const WatchdogUnit& unit, const std::string& name) { return unit.m_name == name; }

	void StartTimer();
	void RestartTimer();
	void RestartTTL();
	void StopTimer();
	void UpdateSettings(const SlotSettings& settings);

	bool Expired();
	int DecrementAndReturnTTL();

	std::string getName() const { return m_name; }
	unsigned int getPID() const { return m_PID; }
	enuActionOnFailure getActionOnFailure() { return m_onFailure; }

private:

	const enuActionOnFailure m_onFailure;
	Timer m_timer;
	ILogger* m_pLogger;
	std::string m_name;
	SlotSettings m_settings;
	unsigned int m_PID;
	int m_TTL;
};

using unitsIterator = std::list<WatchdogUnit>::iterator;

class WatchdogServer
{

public:
	WatchdogServer(const std::string name, ProcessManager* pProcessManager, ILogger* pLogger = NulLogger::getInstance());
	~WatchdogServer();

	void Start();

	void MarkTimerExpired(void* pExpiredTimer_voidptr);
	void SetPeriod_us(unsigned int period_ns);

	ProcessManager* getProcessManager() { return m_pProcessManager; }

	bool timedOut() const { return m_readTimeoutFlag; }
	bool nonblockingReadEmpty() const { return m_nonblockingReadEmptyFlag; }

private:

	std::mutex objectMutex;

	void StartListeningForRequests();
	void InitTimeoutCallback();
	void StartSynchronization(unsigned int timeout_ms, unsigned int BaseTTL);

	void ParseRequest(WatchdogMessage& request);
	void StartCheckingForExpiredUnits();

	void TerminateAll();
	void SendTerminateBroadcast(WatchdogUnit& unit);
	void HandleUnitExpiration(unitsIterator& expiredUnitIter);

	void AddNewUnit(WatchdogMessage& request);
	void RemoveUnit(WatchdogMessage& request);
	unitsIterator RemoveUnit(unitsIterator& unitIter);
	void UpdateSettings(WatchdogMessage& request);
	void KickTimer(WatchdogMessage& request);
	void StartTimer(WatchdogMessage& request);
	void StopTimer(WatchdogMessage& request);
	void SetUnitStatusReadyAndWaiting(WatchdogMessage& request);
	void UnitRequestedTermination(WatchdogMessage& request);

	WatchdogMessage listenForMessage(WatchdogMessage::MessageClass messageClass = WatchdogMessage::MessageClass::ANY,
		enuReceiveOptions options = enuReceiveOptions::NORMAL);

	typedef enum
	{
		Ignore = 0,
		Warn,
		Crash
	} enuActionOnSearchFailure;

	std::list<WatchdogUnit>::iterator getIteratorMatching(const std::string& name, enuActionOnSearchFailure action = Ignore);

	std::string m_name;
	ILogger* m_pLogger;
	ILogger* m_pUnitLogger;
	ProcessManager* m_pProcessManager;

	DataMailbox m_mailbox;
	// TimerCallbackFunctor<WatchdogServer> m_timeoutCallback;

	bool m_terminationFlag = false;
	bool m_readTimeoutFlag = false;
	bool m_nonblockingReadEmptyFlag = false;

	unsigned int m_period_us;

	std::list<WatchdogUnit> m_units;
	// volatile std::list<Timer*> m_expired_pTimers;
};



#endif