#include "WatchdogClient.hpp"
#include "Time.hpp"

#include<sstream>

WatchdogClient::WatchdogClient(const std::string& unitName, ILogger* pLogger)
	: m_unitName(unitName),
	m_serverName(""),
	m_settings({0,0}),
	m_pLogger(pLogger),
	m_PID(getpid()),
	m_mailbox(unitName, pLogger),
	m_status(enuStatus::UNREGISTERED),
	m_server(NO_DESTINATION),
	m_onFailure(enuActionOnFailure::RESET_ONLY)
{
	if (m_pLogger == nullptr)
	{
		m_pLogger = NulLogger::getInstance();
	}

	if (m_unitName == "")
	{
		*m_pLogger << "WatchdogClient - unitName cannot be empty!";
		Kernel::Fatal_Error("WatchdogClient - unitName cannot be empty!");
	}

	*m_pLogger << m_unitName + " - WatchdogClient successfully created";
}

WatchdogClient::~WatchdogClient()
{
	if (m_status != enuStatus::UNREGISTERED)
	{
		Unregister();
	}
}

void WatchdogClient::Register(const std::string& serverName, const SlotSettings& settings, enuActionOnFailure onFailure)
{
	*m_pLogger << m_unitName + " - WatchdogUnit - Registration request to: " + serverName;

	m_onFailure = onFailure;

	if (serverName == "")
	{
		*m_pLogger << m_serverName + " - WatchdogClient - serverName cannot be empty!";
		Kernel::Fatal_Error(m_serverName + " - WatchdogClient - serverName cannot be empty!");
	}

	m_serverName = serverName;

	m_server = MailboxReference(m_serverName + ".server");

	setSettings(settings);

	WatchdogMessage registration_request(m_unitName, m_settings, m_PID, onFailure, WatchdogMessage::REGISTER_REQUEST);
	m_mailbox.send(m_server, &registration_request);
	//m_mailbox.sendConnectionless(m_server, &registration_request);

	waitForServerReply(WatchdogMessage::MessageClass::REGISTER_REPLY, enuReceiveOptions::TIMED, 10);

	*m_pLogger << m_unitName + " - WatchdogUnit - Registration confirmed by: " + m_serverName;

}

void WatchdogClient::setSettings(const SlotSettings& settings)
{
	if (settings.isZero() || settings.timeoutIsZero())
	{
		*m_pLogger << m_unitName + " - WatchdogUnit - registration settings cannot be null!";
		Kernel::Fatal_Error(m_unitName + " - WatchdogUnit - registration settings cannot be null!");
	}

	m_settings = settings;
}

void WatchdogClient::waitForServerReply(WatchdogMessage::MessageClass expectedMessageClass, enuReceiveOptions timedFlag, long timeout_s)
{
	*m_pLogger << "Expecting reponse from: " + m_serverName + " - of class: " + WatchdogMessage::getMessageClassName(expectedMessageClass);

	WatchdogMessage received_response = receiveAndCheckInputMessageFrom(m_server.getName());

	if (received_response.getMessageClass() != expectedMessageClass)
	{
		std::stringstream stringBuilder;

		stringBuilder << m_unitName << " - WatchdogUnit - wrong response from - " << received_response.getSource().getName() << " - on registration request!\n"
			<< "Expected reponse from: " << m_serverName << " - of class: " << WatchdogMessage::getMessageClassName(expectedMessageClass)
			<< " (instead got: " << received_response.getMessageClassName() << " )\n"
			<< "Message info: " << received_response.getInfo();

		*m_pLogger << stringBuilder.str();
		Kernel::Fatal_Error(stringBuilder.str());
	}

	if (received_response.getSource() != m_server)
	{
		*m_pLogger << m_unitName + " - WatchdogUnit - Expected reponse from: " + m_serverName + " instead got from: " + received_response.getSource().getName();
		Kernel::Fatal_Error(m_unitName + " - WatchdogUnit - Expected reponse from: " + m_serverName + " instead got from: " + received_response.getSource().getName());
	}

	*m_pLogger << m_unitName + " - WatchdogUnit - received expected response from: " + m_serverName;

}

WatchdogMessage WatchdogClient::receiveAndCheckInputMessageFrom(const std::string& sourceName, enuReceiveOptions timedFlag, long timeout_ms)
{
	timespec oldSettings = m_mailbox.getTimeout_settings();


	if (timedFlag == enuReceiveOptions::NORMAL)
	{
		timeout_ms = 1; // DUMMY VALUE, IGNORED IF timedFlag IS NOT SET TO TIMED,
						// BUT NOT SET TO ZERO AS NOT TO TRIGGER 0 AND POSSILY NEGATIVE ERRORS IN mailbox SETTINGS (setTimeout_settings)
						// PURELY PRECAUTIONARY MEASURE
	}

	timespec response_timeout_struct = Time::getTimespecFrom_ms(timeout_ms);
	m_mailbox.setTimeout_settings(response_timeout_struct); // does nothing if timedFlag == NORMAL (raises kernel warning?)

	BasicDataMailboxMessage received_message = m_mailbox.receive(timedFlag);

	if (received_message.getDataType() == MessageDataType::TimedOut)
	{
		*m_pLogger << m_unitName + " - WatchdogUnit - no response!";
		Kernel::Fatal_Error(m_unitName + " - WatchdogUnit - no response!");
	}
	else if (received_message.getDataType() != MessageDataType::WatchdogMessage || received_message.getSource().getName() != sourceName)
	{
		std::stringstream stringBuilder;

		stringBuilder << m_unitName << " - WatchdogUnit - wrong response from - " << received_message.getSource().getName() << "!\n"
			<< "Expected reponse from: " << sourceName << " - of type WatchdogMessage (instead got: " << getDataTypeName(received_message.getDataType()) << " )\n"
			<< "Message info: " << received_message.getInfo();

		*m_pLogger << stringBuilder.str();
		Kernel::Fatal_Error(stringBuilder.str());
	}

	WatchdogMessage received_response;
	received_response.Unpack(received_message);

	m_mailbox.setTimeout_settings(oldSettings);

	return received_response;
}

void WatchdogClient::sendSignal(WatchdogMessage::MessageClass messageClass, enuSendOptions sendOptions)
{
	*m_pLogger << m_unitName + " - WatchdogUnit - preparing signal( " + WatchdogMessage::getMessageClassName(messageClass) + " ) for: " + m_serverName;

	// std::cout << "Message class: " << messageClass << std::endl;


	WatchdogMessage signal_message(m_unitName, m_settings, m_PID, m_onFailure, messageClass);

	if (sendOptions % enuSendOptions::NORMAL)
	{
		m_mailbox.send(m_server, &signal_message);
	}
	else if (sendOptions % enuSendOptions::CONNECTIONLESS)
	{
		m_mailbox.sendConnectionless(m_server, &signal_message);
	}
	else
	{
		*m_pLogger << m_unitName + " - WatchdogUnit - signal( " +
			WatchdogMessage::getMessageClassName(messageClass) + " ) for: " + m_serverName + " - could not be sent, invalid options!";

		Kernel::Warning(m_unitName + " - WatchdogUnit - signal( " +
			WatchdogMessage::getMessageClassName(messageClass) + " ) for: " + m_serverName + " - could not be sent, invalid options!");

		return;
	}
	

	*m_pLogger << m_unitName + " - WatchdogUnit - signal( " + WatchdogMessage::getMessageClassName(messageClass) + " ) sent to: " + m_serverName;
}

void WatchdogClient::Unregister()
{
	sendSignal(WatchdogMessage::MessageClass::UNREGISTER_REQUEST, enuSendOptions::CONNECTIONLESS);
}

void WatchdogClient::Start()
{
	sendSignal(WatchdogMessage::MessageClass::START, enuSendOptions::CONNECTIONLESS);
}

void WatchdogClient::Stop()
{
	sendSignal(WatchdogMessage::MessageClass::STOP, enuSendOptions::CONNECTIONLESS);
}

bool WatchdogClient::Kick()
{
	sendSignal(WatchdogMessage::MessageClass::KICK, enuSendOptions::CONNECTIONLESS);

	BasicDataMailboxMessage received_message = m_mailbox.receive(enuReceiveOptions::NONBLOCKING);
	if (received_message.getDataType() == MessageDataType::EmptyQueue)
	{
		*m_pLogger << m_unitName + " - No messages in queue!";
		return true; // NO PENDING MESSAGES (terminate broadcast)
	}
	else if(received_message.getDataType() == MessageDataType::WatchdogMessage)
	{
		WatchdogMessage received_response;
		received_response.Unpack(received_message);

		if (received_response.getMessageClass() == WatchdogMessage::MessageClass::TERMINATE_BROADCAST)
		{
			*m_pLogger << m_unitName + " - Terminate boradcast received!";
			return false;
		}
		else
		{
			*m_pLogger << m_unitName + " - received invalid WatchdogMessage while kicking: " + received_response.getInfo();
			Kernel::Warning(m_unitName + " - received invalid WatchdogMessage while kicking: " + received_response.getInfo());
			return true;
		}
	}

	srand(time(NULL));
	received_message.DumpSerialData("client_invalid_message_" + std::to_string(rand()%1000));
	*m_pLogger << m_unitName + " - received invalid message while kicking: " + received_message.getInfo();
	Kernel::Warning(m_unitName + " - received invalid message while kicking: " + received_message.getInfo());

	return false;
}

void WatchdogClient::Terminate()
{
	sendSignal(WatchdogMessage::MessageClass::TERMINATE_REQUEST, enuSendOptions::CONNECTIONLESS);
}

void WatchdogClient::UpdateSettings(const SlotSettings& settings)
{
	setSettings(settings);
	sendSignal(WatchdogMessage::MessageClass::UPDATE_SETTINGS, enuSendOptions::CONNECTIONLESS);
}

void WatchdogClient::Sync()
{
	// sendSignal(WatchdogMessage::MessageClass::SYNC_REQUEST);

	*m_pLogger << m_unitName + " - WatchdogUnit - waiting for server: " + m_serverName + " - to send `synchronized` signal.";

	waitForServerReply(WatchdogMessage::MessageClass::SYNC_BROADCAST, enuReceiveOptions::CONNECTIONLESS);
}
