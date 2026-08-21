//------------------------------------------------------------------------------------------------
//! Packed authority aero snapshot. Filled from RpcDo_MHJ_FlightState so the
//! owner can restore and replay unacked input instead of open-loop predicting.
//------------------------------------------------------------------------------------------------
class MHJ_FlightState
{
	vector m_vOrigin;
	vector m_vWorldVel;
	float m_fHeading;
	float m_fPitch;
	float m_fBank;
	float m_fBankV;
	float m_fPitchV;
	float m_fPathDeg;
	float m_fPathDegV;
	float m_fOpenT;
	float m_fSimT;
	float m_fTurnFilt;
	float m_fTurnFiltV;
	float m_fPitchInputFilt;
	float m_fPitchInputFiltV;
	int m_iAckSeq;

	//------------------------------------------------------------------------------------------------
	void FromPacket(vector origin, vector worldVelocity, vector ypr, vector auxA, vector auxB, vector auxC, vector auxD, int ackSeq)
	{
		m_vOrigin = origin;
		m_vWorldVel = worldVelocity;
		m_fSimT = auxA[0];
		m_iAckSeq = ackSeq;
		m_fOpenT = auxA[2];
		m_fPathDeg = auxB[0];
		m_fHeading = auxB[1];
		m_fTurnFilt = auxB[2];
		m_fTurnFiltV = auxC[0];
		m_fPitchInputFilt = auxC[1];
		m_fPitchInputFiltV = auxC[2];
		m_fPathDegV = auxD[0];
		m_fBankV = auxD[1];
		m_fPitchV = auxD[2];
		m_fPitch = ypr[1];
		m_fBank = ypr[2];
	}
}
