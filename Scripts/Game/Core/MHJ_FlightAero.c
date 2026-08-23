//------------------------------------------------------------------------------------------------
//! Atmosphere, wind, and ram-air coefficients for MHJ freefall and canopy aero.
//! Density is ISA. Wind is the world weather vector, sheared with altitude.
//------------------------------------------------------------------------------------------------
class MHJ_FlightAero
{
	//------------------------------------------------------------------------------------------------
	static float DensityRatio(float mslY)
	{
		float h = mslY;
		if (h < 0)
			h = 0;
		if (h > 4000)
			h = 4000;

		float theta = 1.0 - 0.0000225577 * h;
		if (theta < 0.55)
			theta = 0.55;
		return Math.Pow(theta, 4.256);
	}

	//------------------------------------------------------------------------------------------------
	//! World-space wind the jumper flies in. Direction is the engine "blows toward" heading.
	static vector WindWorld(float mslY, float time)
	{
		vector wind = vector.Zero;
		float speed = 0;
		float toDeg = 0;

		ChimeraWorld chimeraWorld = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (chimeraWorld)
		{
			TimeAndWeatherManagerEntity weather = chimeraWorld.GetTimeAndWeatherManager();
			if (weather)
			{
				speed = weather.GetWindSpeed();
				toDeg = weather.GetWindDirection();
			}
		}

		if (speed < 0)
			speed = 0;

		float shear = WindShearScale(mslY);
		speed = speed * shear * MHJ_Constants.WIND_AERO_SCALE;

		if (speed > 0.05)
		{
			float gust = 1 + MHJ_Constants.WIND_GUST * Math.PerlinNoise(time * 0.17, 3.4);
			speed = speed * gust;
		}

		vector yaw = vector.Zero;
		yaw[0] = toDeg;
		vector dir = yaw.AnglesToVector();
		wind[0] = dir[0] * speed;
		wind[2] = dir[2] * speed;

		if (speed > 0.4)
		{
			float turb = MHJ_Constants.WIND_TURB * speed;
			wind[0] = wind[0] + Math.PerlinNoise(time * 0.31, 8.1) * turb;
			wind[2] = wind[2] + Math.PerlinNoise(time * 0.27, 11.6) * turb;
		}

		return wind;
	}

	//------------------------------------------------------------------------------------------------
	static float WindShearScale(float mslY)
	{
		float h = mslY;
		if (h < MHJ_Constants.WIND_REF_HEIGHT)
			h = MHJ_Constants.WIND_REF_HEIGHT;

		float s = Math.Pow(h / MHJ_Constants.WIND_REF_HEIGHT, MHJ_Constants.WIND_SHEAR_EXP);
		if (s > MHJ_Constants.WIND_SHEAR_MAX)
			s = MHJ_Constants.WIND_SHEAR_MAX;
		return s;
	}

	//------------------------------------------------------------------------------------------------
	//! 0 during line stretch, overshoots 1 during the snatch, settles to 1.
	static float CanopyInflation(float openT)
	{
		if (openT < MHJ_Constants.CANOPY_STRETCH_TIME)
			return 0;

		if (openT < MHJ_Constants.CANOPY_INFLATE_TIME)
		{
			float span = MHJ_Constants.CANOPY_INFLATE_TIME - MHJ_Constants.CANOPY_STRETCH_TIME;
			if (span < 0.05)
				span = 0.05;
			float u = (openT - MHJ_Constants.CANOPY_STRETCH_TIME) / span;
			if (u < 0)
				u = 0;
			if (u > 1)
				u = 1;
			float e = u * u * (3 - 2 * u);
			return e * MHJ_Constants.CANOPY_SNATCH_OVERSHOOT;
		}

		if (openT < MHJ_Constants.CANOPY_OPEN_TIME)
		{
			float span = MHJ_Constants.CANOPY_OPEN_TIME - MHJ_Constants.CANOPY_INFLATE_TIME;
			if (span < 0.05)
				span = 0.05;
			float u = (openT - MHJ_Constants.CANOPY_INFLATE_TIME) / span;
			if (u < 0)
				u = 0;
			if (u > 1)
				u = 1;
			return MHJ_Constants.CANOPY_SNATCH_OVERSHOOT + (1 - MHJ_Constants.CANOPY_SNATCH_OVERSHOOT) * u;
		}

		return 1;
	}

	//------------------------------------------------------------------------------------------------
	//! Ram-air polar. Hands-off is a descending glide, not a hang. W / a tucked
	//! path lowers Cl so equilibrium TAS rises. S adds Cl. Extra Cl when fast +
	//! braked holds the swoop. High TAS with hands off keeps a lower Cd so dive
	//! energy coasts instead of dying.
	static void CanopyCoeff(float dive, float brake, float tas, float pathDive, out float cl, out float cd)
	{
		float polarDive = dive;
		if (pathDive > polarDive)
			polarDive = pathDive;

		cl = MHJ_Constants.CANOPY_CL_TRIM - polarDive * MHJ_Constants.CANOPY_CL_DIVE_DROP + brake * MHJ_Constants.CANOPY_CL_BRAKE_ADD;
		cd = MHJ_Constants.CANOPY_CD_TRIM + polarDive * MHJ_Constants.CANOPY_CD_DIVE_ADD + brake * MHJ_Constants.CANOPY_CD_BRAKE_ADD;

		float extra = (tas - MHJ_Constants.CANOPY_CRUISE_TAS) / MHJ_Constants.CANOPY_CRUISE_TAS;
		if (extra < 0)
			extra = 0;
		if (extra > 2.2)
			extra = 2.2;
		cl = cl + brake * extra * MHJ_Constants.CANOPY_CL_ENERGY;

		float retain = extra / 2.2;
		float handsOff = 1 - polarDive - brake;
		if (handsOff < 0)
			handsOff = 0;
		cd = cd - retain * handsOff * MHJ_Constants.CANOPY_CD_ENERGY_DROP;

		if (tas < MHJ_Constants.CANOPY_STALL_SPEED)
		{
			float s = (MHJ_Constants.CANOPY_STALL_SPEED - tas) / MHJ_Constants.CANOPY_STALL_SPEED;
			if (s > 1)
				s = 1;
			cl = cl * (1 - s * 0.88);
			cd = cd + s * 0.42;
		}

		if (cl < 0.02)
			cl = 0.02;
		if (cd < 0.02)
			cd = 0.02;
	}

	//------------------------------------------------------------------------------------------------
	//! Slow trim toward cruise TAS only after the path has flattened. Dive accel
	//! still owns a tucked line; brakes keep energy for the swoop.
	static float CanopyCoastBleed(float tas, float dive, float brake, float pathDive, float dt)
	{
		if (dive >= 0.1)
			return tas;
		if (brake >= 0.1)
			return tas;
		if (tas <= MHJ_Constants.CANOPY_CRUISE_TAS)
			return tas;

		float flatten = 1 - pathDive;
		if (flatten < 0.02)
			return tas;

		float extra = tas - MHJ_Constants.CANOPY_CRUISE_TAS;
		tas = tas - extra * MHJ_Constants.CANOPY_RELEASE_BLEED * flatten * dt;
		if (tas < MHJ_Constants.CANOPY_CRUISE_TAS)
			tas = MHJ_Constants.CANOPY_CRUISE_TAS;
		return tas;
	}

	//------------------------------------------------------------------------------------------------
	//! Flight-path angle the wing is asked to fly, degrees. Negative is nose down.
	static float CanopyDemandPathDeg(float dive, float brake)
	{
		float pathDeg = MHJ_Constants.CANOPY_PATH_CRUISE;
		pathDeg = pathDeg + dive * (MHJ_Constants.CANOPY_PATH_DIVE - MHJ_Constants.CANOPY_PATH_CRUISE);
		pathDeg = pathDeg + brake * (MHJ_Constants.CANOPY_PATH_FLARE - MHJ_Constants.CANOPY_PATH_CRUISE);
		if (pathDeg < -MHJ_Constants.DIVE_ANGLE_MAX)
			pathDeg = -MHJ_Constants.DIVE_ANGLE_MAX;
		return pathDeg;
	}

	//------------------------------------------------------------------------------------------------
	static float CanopyPathAlign(float dive, float brake)
	{
		float align = MHJ_Constants.CANOPY_PATH_ALIGN;
		align = align + dive * (MHJ_Constants.CANOPY_PATH_ALIGN_DIVE - MHJ_Constants.CANOPY_PATH_ALIGN);
		align = align + brake * (MHJ_Constants.CANOPY_PATH_ALIGN_FLARE - MHJ_Constants.CANOPY_PATH_ALIGN);
		if (align < 0.4)
			align = 0.4;
		return align;
	}

	//------------------------------------------------------------------------------------------------
	static float EstimateFreefallSeconds(float fallM)
	{
		if (fallM < 1)
			return 0;
		if (fallM < 90)
			return Math.Sqrt(2 * fallM / MHJ_Constants.GRAVITY);

		return 3.6 + (fallM - 80) / 49;
	}

	//------------------------------------------------------------------------------------------------
	//! `toDeg` is the heading the wind blows toward.
	static string WindFromCompass(float toDeg)
	{
		float fromDeg = toDeg + 180;
		while (fromDeg >= 360)
			fromDeg = fromDeg - 360;
		while (fromDeg < 0)
			fromDeg = fromDeg + 360;

		int idx = (fromDeg + 22.5) / 45;
		if (idx < 0)
			idx = 0;
		if (idx > 7)
			idx = 0;

		if (idx == 0)
			return "N";
		if (idx == 1)
			return "NE";
		if (idx == 2)
			return "E";
		if (idx == 3)
			return "SE";
		if (idx == 4)
			return "S";
		if (idx == 5)
			return "SW";
		if (idx == 6)
			return "W";
		return "NW";
	}

	//------------------------------------------------------------------------------------------------
	static string WindRelativeLabel(float windSpeed, float headingDot)
	{
		if (windSpeed < 0.8)
			return "CALM";
		if (headingDot > 0.45)
			return "TAILWIND";
		if (headingDot < -0.45)
			return "HEADWIND";
		return "CROSSWIND";
	}

	//------------------------------------------------------------------------------------------------
	static string FormatWindStatus(vector wind)
	{
		float spd = wind.Length();
		if (spd < 0.8)
			return "Wind calm.";

		float toDeg = Math.Atan2(wind[0], wind[2]) * Math.RAD2DEG;
		int spdM = Math.Round(spd);
		return "Wind " + spdM.ToString() + " m/s from " + WindFromCompass(toDeg) + ".";
	}
}
