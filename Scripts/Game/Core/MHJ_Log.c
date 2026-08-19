//------------------------------------------------------------------------------------------------
//! Print wrapper prefixed [MHJ].
//------------------------------------------------------------------------------------------------
class MHJ_Log
{
	protected static const string PREFIX = "[MHJ]";
	//! Flight/board lifecycle diagnostics. Keep enabled until dedicated boarding
	//! has completed reliably; these are transition logs, not per-frame spam.
	protected static const bool DIAG = true;

	//------------------------------------------------------------------------------------------------
	static bool IsDiag()
	{
		return DIAG;
	}

	//------------------------------------------------------------------------------------------------
	static void Info(string message)
	{
		if (!DIAG)
			return;

		Print(PREFIX + " " + message, LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	static void Land(string message)
	{
		Print(PREFIX + " LAND " + message, LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	static void Warning(string message)
	{
		Print(PREFIX + " " + message, LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	static string Flag(bool value)
	{
		if (value)
			return "1";
		return "0";
	}

	//------------------------------------------------------------------------------------------------
	static string Deg(float value)
	{
		return value.ToString();
	}

	//------------------------------------------------------------------------------------------------
	static string Ypr(vector ypr)
	{
		return Deg(ypr[0]) + "," + Deg(ypr[1]) + "," + Deg(ypr[2]);
	}

	//------------------------------------------------------------------------------------------------
	static void Error(string message)
	{
		Print(PREFIX + " " + message, LogLevel.ERROR);
	}
}
