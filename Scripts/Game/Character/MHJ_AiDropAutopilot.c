//------------------------------------------------------------------------------------------------
//! Server canopy step for an AI drop slot. Homes on a personal LZ. Reuses
//! MHJ_FlightAero coefficients; does not touch player canopy flight.
//------------------------------------------------------------------------------------------------
class MHJ_AiDropAutopilot
{
	//------------------------------------------------------------------------------------------------
	static void InitSlot(notnull MHJ_AiDropSlot slot, notnull ChimeraCharacter jumper, vector lz)
	{
		slot.m_Character = jumper;
		slot.m_vOrigin = jumper.GetOrigin();
		slot.m_vLz = lz;
		slot.m_fOpenT = MHJ_Constants.CANOPY_OPEN_TIME;
		slot.m_fSimT = 0;
		slot.m_fPathDeg = MHJ_Constants.CANOPY_PATH_CRUISE;
		slot.m_fPathDegV = 0;
		slot.m_fTurnFilt = 0;
		slot.m_fTurnFiltV = 0;
		slot.m_fPitchFilt = 0;
		slot.m_fPitchFiltV = 0;
		slot.m_bActive = true;

		vector toLz = lz - slot.m_vOrigin;
		toLz[1] = 0;
		if (toLz.Length() < 0.5)
			slot.m_fHeading = jumper.GetYawPitchRoll()[0] * Math.DEG2RAD;
		else
			slot.m_fHeading = Math.Atan2(toLz[0], toLz[2]);

		vector forward = HeadingForward(slot.m_fHeading);
		float pathRad = MHJ_Constants.CANOPY_PATH_CRUISE * Math.DEG2RAD;
		slot.m_vVel = forward * (MHJ_Constants.CANOPY_CRUISE_TAS * Math.Cos(-pathRad));
		slot.m_vVel[1] = -MHJ_Constants.CANOPY_MAX_SINK;
		ApplyAiSpeed(slot);
		slot.m_vWind = MHJ_FlightAero.WindWorld(slot.m_vOrigin[1], 0);
	}

	//------------------------------------------------------------------------------------------------
	static void Step(notnull MHJ_AiDropSlot slot, float dt)
	{
		if (dt <= 0)
			return;

		slot.m_fSimT = slot.m_fSimT + dt;
		slot.m_fOpenT = slot.m_fOpenT + dt;
		slot.m_vWind = MHJ_FlightAero.WindWorld(slot.m_vOrigin[1], slot.m_fSimT);

		float agl = GetAgl(slot.m_vOrigin);
		float turn;
		float pitch;
		ComputeInput(slot, agl, turn, pitch);

		slot.m_fTurnFilt = Math.SmoothCD(slot.m_fTurnFilt, turn, slot.m_fTurnFiltV, MHJ_Constants.STEER_FILTER_TIME, MHJ_Constants.STEER_FILTER_MAX, dt);
		slot.m_fPitchFilt = Math.SmoothCD(slot.m_fPitchFilt, pitch, slot.m_fPitchFiltV, MHJ_Constants.CANOPY_PITCH_IN_TIME, MHJ_Constants.CANOPY_PITCH_IN_MAX, dt);

		float dive = 0;
		float brake = 0;
		if (slot.m_fPitchFilt > 0)
			dive = slot.m_fPitchFilt;
		if (slot.m_fPitchFilt < 0)
			brake = -slot.m_fPitchFilt;

		slot.m_fHeading = WrapHeading(slot.m_fHeading + slot.m_fTurnFilt * MHJ_Constants.CANOPY_TURN_RATE * dt);

		float wantedPath = MHJ_FlightAero.CanopyDemandPathDeg(dive, brake);
		slot.m_fPathDeg = Math.SmoothCD(slot.m_fPathDeg, wantedPath, slot.m_fPathDegV, MHJ_Constants.CANOPY_PATH_INERTIA, MHJ_Constants.CANOPY_PATH_IN_MAX, dt);

		Integrate(slot, dt, dive, brake);
		slot.m_vOrigin = slot.m_vOrigin + slot.m_vVel * dt;
	}

	//------------------------------------------------------------------------------------------------
	static void ComputeInput(notnull MHJ_AiDropSlot slot, float agl, out float turn, out float pitch)
	{
		turn = 0;
		pitch = 0;

		vector toLz = slot.m_vLz - slot.m_vOrigin;
		toLz[1] = 0;
		float dist = toLz.Length();
		if (dist > 2)
		{
			float desired = Math.Atan2(toLz[0], toLz[2]);
			float err = WrapHeading(desired - slot.m_fHeading);
			float span = 45 * Math.DEG2RAD;
			turn = err / span;
			if (turn > 1)
				turn = 1;
			if (turn < -1)
				turn = -1;
		}

		if (agl < MHJ_Constants.FLARE_AGL)
		{
			float flareSpan = MHJ_Constants.FLARE_AGL - MHJ_Constants.FLARE_END_AGL;
			if (flareSpan < 0.5)
				flareSpan = 0.5;
			float autoFlare = 1 - (agl - MHJ_Constants.FLARE_END_AGL) / flareSpan;
			if (autoFlare < 0)
				autoFlare = 0;
			if (autoFlare > 1)
				autoFlare = 1;
			pitch = -autoFlare;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void Integrate(notnull MHJ_AiDropSlot slot, float dt, float dive, float brake)
	{
		float density = MHJ_FlightAero.DensityRatio(slot.m_vOrigin[1]);
		slot.m_vVel[1] = slot.m_vVel[1] - MHJ_Constants.GRAVITY * dt;

		vector airVelocity = slot.m_vVel - slot.m_vWind;
		float trueAirspeed = airVelocity.Length();
		if (trueAirspeed < 0.35)
			return;

		vector airDirection = airVelocity;
		airDirection.Normalize();

		vector nose = PathNose(slot.m_fHeading, slot.m_fPathDeg);
		float blend = MHJ_FlightAero.CanopyPathAlign(dive, brake) * dt;
		if (blend > 1)
			blend = 1;

		vector mixed = airDirection + (nose - airDirection) * blend;
		if (mixed.Length() < 0.001)
			return;
		mixed.Normalize();

		float pathDive = (-slot.m_fPathDeg) / MHJ_Constants.DIVE_ANGLE_MAX;
		if (pathDive < 0)
			pathDive = 0;
		if (pathDive > 1)
			pathDive = 1;

		trueAirspeed = trueAirspeed + MHJ_Constants.CANOPY_DIVE_ACCEL * pathDive * dt;
		trueAirspeed = MHJ_FlightAero.CanopyCoastBleed(trueAirspeed, dive, brake, pathDive, dt);
		slot.m_vVel = mixed * trueAirspeed + slot.m_vWind;

		float liftCoefficient;
		float dragCoefficient;
		MHJ_FlightAero.CanopyCoeff(dive, brake, trueAirspeed, pathDive, liftCoefficient, dragCoefficient);

		float dynamicPressure = 0.5 * MHJ_Constants.AIR_DENSITY_SL * density * trueAirspeed * trueAirspeed;
		float liftForce = dynamicPressure * liftCoefficient * MHJ_Constants.CANOPY_AREA;
		float dragForce = dynamicPressure * dragCoefficient * MHJ_Constants.CANOPY_AREA;
		float inverseMass = 1 / MHJ_Constants.MASS;

		vector right = HeadingRight(slot.m_fHeading);
		vector liftDirection = SCR_Math3D.Cross(airDirection, right);
		if (liftDirection.Length() < 0.05)
			liftDirection = Vector(0, 1, 0);
		liftDirection.Normalize();
		if (vector.Dot(liftDirection, Vector(0, 1, 0)) < 0)
			liftDirection = liftDirection * -1;

		slot.m_vVel = slot.m_vVel + liftDirection * (liftForce * inverseMass * dt);
		slot.m_vVel = slot.m_vVel - airDirection * (dragForce * inverseMass * dt);
		if (slot.m_vVel[1] > MHJ_Constants.CANOPY_MAX_CLIMB)
			slot.m_vVel[1] = MHJ_Constants.CANOPY_MAX_CLIMB;

		float speed = slot.m_vVel.Length();
		if (speed > MHJ_Constants.CANOPY_MAX_TAS)
			slot.m_vVel = slot.m_vVel * (MHJ_Constants.CANOPY_MAX_TAS / speed);

		ApplyAiSpeed(slot);
	}

	//------------------------------------------------------------------------------------------------
	//! Shared polar still writes player cruise/sink. Scale after that write so
	//! CoastBleed cannot restore full TAS on the next line.
	protected static void ApplyAiSpeed(notnull MHJ_AiDropSlot slot)
	{
		slot.m_vVel[0] = slot.m_vVel[0] * MHJ_Constants.AI_CANOPY_SPEED_SCALE;
		slot.m_vVel[2] = slot.m_vVel[2] * MHJ_Constants.AI_CANOPY_SPEED_SCALE;

		float sink = slot.m_vVel[1];
		if (sink < 0)
			sink = sink * MHJ_Constants.AI_CANOPY_SINK_SCALE;

		float maxSink = -MHJ_Constants.CANOPY_MAX_SINK * MHJ_Constants.AI_CANOPY_SINK_SCALE;
		if (sink < maxSink)
			sink = maxSink;

		slot.m_vVel[1] = sink;
	}

	//------------------------------------------------------------------------------------------------
	static float GetAgl(vector origin)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return origin[1];

		float terrainY = world.GetSurfaceY(origin[0], origin[2]);
		float agl = origin[1] - terrainY;
		if (agl < 0)
			agl = 0;
		return agl;
	}

	//------------------------------------------------------------------------------------------------
	static float WrapHeading(float heading)
	{
		while (heading > Math.PI)
			heading = heading - Math.PI2;
		while (heading < -Math.PI)
			heading = heading + Math.PI2;
		return heading;
	}

	//------------------------------------------------------------------------------------------------
	static vector HeadingForward(float heading)
	{
		return Vector(Math.Sin(heading), 0, Math.Cos(heading));
	}

	//------------------------------------------------------------------------------------------------
	static vector HeadingRight(float heading)
	{
		return Vector(Math.Cos(heading), 0, -Math.Sin(heading));
	}

	//------------------------------------------------------------------------------------------------
	protected static vector PathNose(float heading, float pathDeg)
	{
		float pathRad = pathDeg * Math.DEG2RAD;
		vector forward = HeadingForward(heading);
		vector nose = forward * Math.Cos(-pathRad);
		nose[1] = Math.Sin(pathRad);
		if (nose.Length() < 0.001)
			return forward;
		nose.Normalize();
		return nose;
	}
}
