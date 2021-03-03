#ifndef WATCHDOG_SETTINGS_HPP
#define WATCHDOG_SETTINGS_HPP

struct SlotSettings;

bool operator==(const SlotSettings& s1, const SlotSettings& s2);

struct SlotSettings
{
    unsigned int m_BaseTTL;
    unsigned int m_timeout_ms;

	bool timeoutIsZero() const { return m_timeout_ms == 0; }
	bool isZero() const { return *this == (SlotSettings) { 0, 0 }; }
};

enum class enuActionOnFailure
{
	RESET_ONLY = 0,
	KILL_ALL
};

#endif