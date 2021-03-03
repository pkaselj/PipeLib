#include "WatchdogServer.hpp"

#include <algorithm>
#include <thread>
#include <sstream>

#include <csignal>

// #define Timer_fPointer TimerCallbackFunctor<WatchdogServer>::TfPointer

// #define VOLATILE_pExpiredpTimers ((std::list<Timer*>*) & m_expired_pTimers)

WatchdogServer::WatchdogServer(const std::string name, ProcessManager* pProcessManager, ILogger* pLogger)
	:	m_mailbox(name + ".server", pLogger), // DEBUG
	m_pLogger(pLogger),
	m_name(name),
	objectMutex(),
	// m_timeoutCallback(this, (Timer_fPointer) &WatchdogServer::MarkTimerExpired),
	m_terminationFlag(false),
	m_period_us(100 * Time::ms_to_us),
	m_pProcessManager(pProcessManager)

{
	if (m_pLogger == nullptr)
	{
		m_pLogger = NulLogger::getInstance();
	}

	if (m_name == "")
	{
		*m_pLogger << "WatchdogServer name cannot be empty!";
		Kernel::Fatal_Error("WatchdogServer name cannot be empty!");
	}

	if (m_pProcessManager == nullptr)
	{
		*m_pLogger << m_name + " - WatchdogServer must have process manager.";
		Kernel::Fatal_Error(m_name + " - WatchdogServer must have process manager.");
	}

	m_units.clear();

	// VOLATILE_pExpiredpTimers->clear();

	InitTimeoutCallback();
}

WatchdogServer::~WatchdogServer()
{
	m_units.clear();

	// ((std::list<Timer*>*) & m_expired_pTimers)->clear();
}

void WatchdogServer::Start()
{
	// StartSynchronization(200, 10);

	std::thread requestParserThread(&WatchdogServer::StartListeningForRequests, this);
	std::thread unitWatcherThread(&WatchdogServer::StartCheckingForExpiredUnits, this);

	requestParserThread.join();
	unitWatcherThread.join();
	
	/*while (m_terminationFlag == false)
	{
		StartCheckingForExpiredUnits();
		StartListeningForRequests();
	}*/


	// *m_pLogger << m_name + " - Terminating all units w Process Manager!";
	// m_pProcessManager->killAll();
	// m_units.clear();
	TerminateAll();
	*m_pLogger << m_name + " - Termination!";
}

void WatchdogServer::StartListeningForRequests()
{
	while (m_terminationFlag == false)
	{
		m_mailbox.setRTO_ns(m_period_us * Time::us_to_ns);

		WatchdogMessage received_request = listenForMessage(WatchdogMessage::ANY, enuReceiveOptions::TIMED);

		if (m_readTimeoutFlag == true)
		{
			m_readTimeoutFlag = false;
			continue;
		}

		// TODO New thread?
		// std::lock_guard<std::mutex> lock(objectMutex);
		ParseRequest(received_request);
	}

}

WatchdogMessage WatchdogServer::listenForMessage(WatchdogMessage::MessageClass messageClass, enuReceiveOptions options)
{
	m_readTimeoutFlag = false;
	m_nonblockingReadEmptyFlag = false;

	BasicDataMailboxMessage received_message = m_mailbox.receive(options);

	if (received_message.getDataType() == MessageDataType::TimedOut)
	{
		m_readTimeoutFlag = true;
		return WatchdogMessage(WatchdogMessage::MessageClass::NONE);
	}

	else if (received_message.getDataType() == MessageDataType::EmptyQueue)
	{
		m_nonblockingReadEmptyFlag = true;
		return WatchdogMessage(WatchdogMessage::MessageClass::NONE);
	}

	else if (received_message.getDataType() != MessageDataType::WatchdogMessage)
	{
		*m_pLogger << m_name + " - WatchdogServer received invalid type of message: MessageDataType =  " + getDataTypeName(received_message.getDataType());
		Kernel::Fatal_Error(m_name + " - WatchdogServer received invalid type of message: MessageDataType =  " + getDataTypeName(received_message.getDataType()));
	}

	WatchdogMessage received_message_parsed;
	received_message_parsed.Unpack(received_message);

	*m_pLogger << m_name + " - WatchdogMessage received: " + received_message_parsed.getInfo();

	return received_message_parsed;
}

void WatchdogServer::StartSynchronization(unsigned int timeout_ms, unsigned int BaseTTL)
{
	*m_pLogger << m_name + " - Watchdog sync period start.";

	// Wait for the first unit to register
	WatchdogMessage received_request = listenForMessage(WatchdogMessage::REGISTER_REQUEST);
	AddNewUnit(received_request);


	timespec oldTimeoutSettings = m_mailbox.getTimeout_settings();
	m_mailbox.setRTO_ns(timeout_ms * Time::ms_to_ns);

	// set TTL to BaseTTL
	// every timeout decrease TTL
	// every message == REGISTER_REQUEST restart TTL to BaseTTL
	unsigned int TTL = BaseTTL;

	do
	{
		*m_pLogger << m_name + " - Sync period TTL left: " + std::to_string(TTL);

		WatchdogMessage received_request = listenForMessage(WatchdogMessage::REGISTER_REQUEST, enuReceiveOptions::TIMED);
		if (m_readTimeoutFlag == true)
		{
			m_readTimeoutFlag = false;
			continue;
		}

		// else
		// reset TTL
		TTL = BaseTTL + 1; // +1 to compensate for --TTL at the end of the loop iteration
		AddNewUnit(received_request);

	} while (--TTL > 0);

	*m_pLogger << m_name + " - Sync listen period over. Sending broadcast signals";

	// Send SYNC_BROADCAST to all connected units (CONNECTIONLESS)

	WatchdogMessage sync_broadcast(m_name, WatchdogMessage::SYNC_BROADCAST);
	for (auto& unit : m_units)
	{
		MailboxReference destination(unit.getName());
		m_mailbox.sendConnectionless(destination, &sync_broadcast);
		*m_pLogger << m_name + " - Sync broadcast sent to " + unit.getName();
	}

	// Reset mailbox timeout settings
	m_mailbox.setTimeout_settings(oldTimeoutSettings);

	*m_pLogger << m_name + " - Sync period over. Broadcast signals sent";
}

void WatchdogServer::SetPeriod_us(unsigned int period_us)
{
	m_period_us = period_us;
}

void WatchdogServer::ParseRequest(WatchdogMessage& request)
{

	using MessageClass = WatchdogMessage::MessageClass;
	switch (request.getMessageClass())
	{
	case MessageClass::REGISTER_REQUEST:
	{
		std::lock_guard<std::mutex> lock(objectMutex);
		AddNewUnit(request);
	}
		break;
	case MessageClass::UNREGISTER_REQUEST:
	{
 		std::lock_guard<std::mutex> lock(objectMutex);
		RemoveUnit(request);
	}
		break;
	case MessageClass::UPDATE_SETTINGS:
		UpdateSettings(request);
		break;
	case MessageClass::KICK:
		KickTimer(request);
		break;
	case MessageClass::START:
		StartTimer(request);
		break;
	case MessageClass::STOP:
		StopTimer(request);
		break;
	case MessageClass::SYNC_REQUEST:
		SetUnitStatusReadyAndWaiting(request);
		break;
	case MessageClass::TERMINATE_REQUEST:
		UnitRequestedTermination(request);
		break;
	default:
		// ERROR TODO
		*m_pLogger << m_name + " - Watchdog Server received invalid WatchdogMessage: " + request.getInfo();
		Kernel::Warning(m_name + " - Watchdog Server received invalid WatchdogMessage: " + request.getInfo());
		break;

	}
}

// Deprecated
void WatchdogServer::MarkTimerExpired(void* pExpiredTimer_voidptr)
{
	/*
	
	Timer* pExpiredTimer = (Timer*)pExpiredTimer_voidptr;
	// std::cout << "Timer expired - WDS " << pExpiredTimer->getName() << std::endl;
	std::cout << "Timer expired - WDS, timer address: " << std::hex << (unsigned int)pExpiredTimer << " PID " << getpid() << std::endl;
	std::cout << "Timer status: " << pExpiredTimer->getTimerStatus() << std::endl;

	*/

	*m_pLogger << m_name +  " - Timer expired!";
}

void WatchdogServer::StartCheckingForExpiredUnits()
{
	 while (m_terminationFlag == false) // MT only
	{
		//std::cout << "CHECK" << std::endl;
		 usleep(m_period_us); // only on MT

		// TODO mutex lock?
 		std::unique_lock<std::mutex> mutexLock(objectMutex);
		for (auto unit = m_units.begin(); unit != m_units.end(); ++unit)
		{
			//std::cout << "UNIT" << std::endl;

			if (unit->Expired())
			{
				//std::cout << "Expired" << std::endl;
				int remainingTTL = unit->DecrementAndReturnTTL();


				std::stringstream stringBuilder;
				stringBuilder << "Unit named: " << unit->getName() << " expired!" << "\n"
					<< "Process ID: " << unit->getPID() << "\n"
					<< "Remaining TTL: " << remainingTTL << "\n"
					<< "termination flag: " << std::to_string(m_terminationFlag);

				*m_pLogger << stringBuilder.str();

				if (remainingTTL == 0)
				{
					//std::cout << "Terminate" << std::endl;
					// m_terminationFlag = true;
					HandleUnitExpiration(unit);

					// raise(SIGINT);

					if (m_terminationFlag == true)
					{
						/*
						* If TerminateAll() is called because the expired unit
						* is a critical system i.e. enuActionOnFailure::KILL_ALL,
						* TerminateAll() sets m_terminationFlag and erases all
						* units in m_units list and in doing so
						* invalidates the current unit iterator. This prevents it from
						* using it and iterating.
						*/
						return;
					}
				}
				else
				{
					//std::cout << "Reset" << std::endl;
					unit->RestartTimer();
				}
			}
		}
		mutexLock.unlock();
	}

}

void WatchdogServer::HandleUnitExpiration(unitsIterator& expiredUnitIter)
{
	if (expiredUnitIter->getActionOnFailure() == enuActionOnFailure::RESET_ONLY)
	{
		int processPID = expiredUnitIter->getPID();

		expiredUnitIter = RemoveUnit(expiredUnitIter);

		m_pProcessManager->resetProcess(processPID);
	}
	else if(expiredUnitIter->getActionOnFailure() == enuActionOnFailure::KILL_ALL)
	{

		// TerminateAll();
		// m_pProcessManager->killAll();
		m_terminationFlag = true;
	}
}

void WatchdogServer::TerminateAll()
{
	*m_pLogger << m_name + " - Terminate all!";
	m_terminationFlag = true;

	while (m_units.empty() == false)
	{
		WatchdogUnit* pUnit = &m_units.front();
		m_units.pop_front();

		pUnit->StopTimer();
		// SendTerminateBroadcast(*pUnit);
	}

	// sleep(5);
	m_pProcessManager->killAll();
}

void WatchdogServer::SendTerminateBroadcast(WatchdogUnit& unit)
{
	*m_pLogger << m_name + " - Terminating unit: " + unit.getName();

	WatchdogMessage terminate_broadcast(m_name, WatchdogMessage::TERMINATE_BROADCAST);
	MailboxReference destination(unit.getName());

	m_mailbox.sendConnectionless(destination, &terminate_broadcast);

	srand(time(NULL));
	terminate_broadcast.DumpSerialData("server_terminate_broadcast_" + std::to_string(rand() % 1000));

	*m_pLogger << m_name + " - Terminate broadcast sent to unit: " + unit.getName();
}

void WatchdogServer::InitTimeoutCallback()
{
	// DEPRECATED -- BUGGY
	// Timer::setTimeoutCallback(&m_timeoutCallback);
	// *m_pLogger << "Timeout callback set!";
	
	Timer::ignoreAlarmSignals();
	//Timer::setTimeoutCallback(&m_timeoutCallback);
}

void WatchdogServer::AddNewUnit(WatchdogMessage& request)
{
	*m_pLogger << m_name + " - WatchdogServer - requested addition of unit: " + request.getName();

	auto matchesName = [&](WatchdogUnit& unit) {return unit.getName() == request.getName();};
	auto position = std::find_if(m_units.begin(), m_units.end(), matchesName);


	if (position != m_units.end())
	{

		*m_pLogger << m_name + " - WatchdogServer Unit tries to register itself multiple times. Request info: " + request.getInfo();
		Kernel::Fatal_Error(m_name + " - WatchdogServer Unit tries to register itself multiple times. Request info: " + request.getInfo());
		return;
	}


	std::stringstream stringBuilder;

	stringBuilder << "Name: " << request.getName() << "\n"
		<< "\tTimeout(ms): " << request.getSettings().m_timeout_ms << "\n"
		<< "\tBase TTL: " << request.getSettings().m_BaseTTL << "\n"
		<< "\tPID: " << request.getPID() << "\n"
		<< "\tAction on failure code: " << (int)request.getActionOnFailure() << "\n"
		<< "\tLogger pointer address: " << std::hex << (int)m_pLogger;

	*m_pLogger << stringBuilder.str();


	m_units.push_back(
		WatchdogUnit(request.getName(),
		request.getSettings(),
		request.getPID(),
		request.getActionOnFailure(),
		m_pLogger)
	);

	*m_pLogger << m_name + " - WatchdogServer - unit matching request: " + request.getName() + " - added!";

	WatchdogMessage register_reply(m_name, WatchdogMessage::REGISTER_REPLY);
	m_mailbox.sendConnectionless(request.getSource(), &register_reply);

	*m_pLogger << m_name + " - WatchdogServer - sent register ACK to unit: " + request.getName();

}

unitsIterator WatchdogServer::RemoveUnit(unitsIterator& unitIter)
{
	auto unitToBeErasedIter = unitIter--;

	unitToBeErasedIter->StopTimer();
	m_units.erase(unitToBeErasedIter);

	return unitIter;
}

void WatchdogServer::RemoveUnit(WatchdogMessage& request)
{
	*m_pLogger << m_name + " - WatchdogServer - requested removal of unit: " + request.getName();

	auto equalsRequestName = [&](WatchdogUnit& unit) {return unit == request.getName(); };
	auto position = std::find_if(m_units.begin(), m_units.end(), equalsRequestName);


	if (position == m_units.end())
	{
		*m_pLogger << m_name + " - WatchdogServer - no WatchdogUnits matching request: " + request.getName() + " - found! No units removed!";
		Kernel::Warning(m_name + " - WatchdogServer - no WatchdogUnits matching request: " + request.getName() + " - found! No units removed!");
		return;
	}

	RemoveUnit(position);

	*m_pLogger << m_name + " - WatchdogServer - unit matching request: " + request.getName() + " - removed!";
}

void WatchdogServer::UpdateSettings(WatchdogMessage& request)
{
	*m_pLogger << m_name + " - WatchdogServer - request to update settings: " + request.getInfo();

	auto position = getIteratorMatching(request.getName(), enuActionOnSearchFailure::Crash);

	position->UpdateSettings(request.getSettings());
}

void WatchdogServer::KickTimer(WatchdogMessage& request)
{
	*m_pLogger << m_name + " - WatchdogServer - request to kick timer: " + request.getName();

	auto position = getIteratorMatching(request.getName(), enuActionOnSearchFailure::Warn);

	position->RestartTTL();
	position->RestartTimer();
}

void WatchdogServer::StartTimer(WatchdogMessage& request)
{
	*m_pLogger << m_name + " - WatchdogServer - request to start timer: " + request.getName();

	auto position = getIteratorMatching(request.getName(), enuActionOnSearchFailure::Crash);

	position->StartTimer();
}

void WatchdogServer::StopTimer(WatchdogMessage& request)
{
	*m_pLogger << m_name + " - WatchdogServer - request to stop timer: " + request.getName();

	auto position = getIteratorMatching(request.getName(), enuActionOnSearchFailure::Warn);

	position->StopTimer();
}

void WatchdogServer::SetUnitStatusReadyAndWaiting(WatchdogMessage& request)
{
	*m_pLogger << m_name + " - WatchdogServer - sync request from: " + request.getName();

	auto position = getIteratorMatching(request.getName(), enuActionOnSearchFailure::Ignore);

	// TODO
}
void WatchdogServer::UnitRequestedTermination(WatchdogMessage& request)
{
	*m_pLogger << m_name + " - WatchdogServer - terminate request from: " + request.getName();

	auto position = getIteratorMatching(request.getName(), enuActionOnSearchFailure::Ignore);

	// TODO
}

std::list<WatchdogUnit>::iterator WatchdogServer::getIteratorMatching(const std::string& name, enuActionOnSearchFailure action)
{
	auto matchesName = [&](WatchdogUnit& unit) {return unit == name;};
	auto position = std::find_if(m_units.begin(), m_units.end(), matchesName);

	if (position == m_units.end())
	{
		std::string error_string = m_name + " - WatchdogServer - unit matching name: " + name + " - not found.";
		*m_pLogger << error_string;

		if (action == enuActionOnSearchFailure::Warn)
		{
			Kernel::Warning(error_string);
		}
		else if (action == enuActionOnSearchFailure::Crash)
		{
			Kernel::Fatal_Error(error_string);
		}
		
	}

	return position;
}



WatchdogUnit::WatchdogUnit(const std::string& name, const SlotSettings& settings, unsigned int PID, enuActionOnFailure onFailure, ILogger* pLogger)
	:	m_name(name), m_settings(settings), m_pLogger(pLogger), m_timer(name + std::string(".timer"), pLogger), m_PID(PID), m_onFailure(onFailure)
{

	if (m_pLogger == nullptr)
	{
		m_pLogger = NulLogger::getInstance();
	}

	if (m_name == "")
	{
		*m_pLogger << "WatchdogUnit name cannot be empty!";
		Kernel::Fatal_Error("WatchdogUnit name cannot be empty!");
	}

	if (m_settings.m_BaseTTL == 0 || m_settings.m_timeout_ms == 0)
	{
		std::string error_string = "WatchdogUnit: " + m_name + " timeout settings are invalid!\n" +
			"\tBaseTTL = " + std::to_string(m_settings.m_BaseTTL) + "\n" +
			"\tTimeout_ms = " + std::to_string(m_settings.m_timeout_ms) + "\n";

		*m_pLogger << error_string;
		Kernel::Fatal_Error(error_string);
	}

	m_TTL = m_settings.m_BaseTTL;
	m_timer.setTimeout_ms(m_settings.m_timeout_ms);
}

WatchdogUnit::~WatchdogUnit()
{
	m_timer.Stop();
}

void WatchdogUnit::StartTimer()
{
	m_timer.clearTimeoutSettings();
	m_timer.setTimeout_ms(m_settings.m_timeout_ms);
	m_timer.Start();
}

void WatchdogUnit::RestartTimer()
{
	m_timer.Reset();
}

void WatchdogUnit::RestartTTL()
{
	m_TTL = m_settings.m_BaseTTL;
}

void WatchdogUnit::StopTimer()
{
	m_timer.Stop();
}

void WatchdogUnit::UpdateSettings(const SlotSettings& settings)
{
	m_settings = settings;
	m_timer.setTimeout_ms(m_settings.m_timeout_ms);

	// ?
	m_TTL = m_settings.m_BaseTTL;
}

bool WatchdogUnit::Expired()
{
	return m_timer.getTimerStatus() == Timer::enuTimerStatus::Expired;
}

int WatchdogUnit::DecrementAndReturnTTL()
{
	if (m_TTL != 0)
	{
		--m_TTL;
	}

	return m_TTL;
}




