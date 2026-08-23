//------------------------------------------------------------------------------------------------
//! Server-authoritative AI drop stick. One script-spawned entity drives every
//! jumper with autopilot. Clients spawn local-only canopy meshes from jumper
//! RplIds. Do not use a hand-authored prefab GUID — Workbench never registered
//! those, so RPL insert failed and the visual remaps died.
//------------------------------------------------------------------------------------------------
class MHJ_AiDropDirectorClass : GenericEntityClass
{
}

class MHJ_AiDropDirector : GenericEntity
{
	protected static MHJ_AiDropDirector s_Live;
	protected static ref array<RplId> s_aRemoteIds;
	protected static ref array<IEntity> s_aRemoteVisuals;
	protected static bool s_bRemoteTick;

	protected ref array<ref MHJ_AiDropSlot> m_aSlots;
	protected ref array<RplId> m_aClientIds;
	protected ref array<IEntity> m_aVisuals;
	protected ref ScriptInvoker m_OnJumperFinished;
	protected bool m_bClosing;
	protected static bool s_bLoggedVisualY;

	//------------------------------------------------------------------------------------------------
	void MHJ_AiDropDirector(IEntitySource src, IEntity parent)
	{
		SetEventMask(EntityEvent.INIT | EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	void ~MHJ_AiDropDirector(IEntitySource src, IEntity parent)
	{
		ClearClientVisuals();
		if (s_Live == this)
			s_Live = null;
	}

	//------------------------------------------------------------------------------------------------
	static MHJ_AiDropDirector GetLive()
	{
		return s_Live;
	}

	//------------------------------------------------------------------------------------------------
	static bool HasLiveStick()
	{
		if (!s_Live)
			return false;
		return s_Live.HasActiveSlots();
	}

	//------------------------------------------------------------------------------------------------
	static bool IsActiveJumper(IEntity character)
	{
		if (!s_Live || !character)
			return false;
		return s_Live.FindSlot(character) != null;
	}

	//------------------------------------------------------------------------------------------------
	static MHJ_AiDropDirector SpawnStick(vector lz)
	{
		if (!Replication.IsServer())
			return null;
		if (s_Live && s_Live.HasActiveSlots())
		{
			MHJ_Log.Warning("AI drop stick refused: one stick is already live");
			return null;
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return null;

		ref EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		spawnParams.Transform[3] = lz;

		MHJ_AiDropDirector director = MHJ_AiDropDirector.Cast(GetGame().SpawnEntity(MHJ_AiDropDirector, world, spawnParams));
		if (!director)
			return null;

		s_Live = director;
		MHJ_Log.Info("AI drop stick spawned at " + lz.ToString());
		return director;
	}

	//------------------------------------------------------------------------------------------------
	ScriptInvoker GetOnJumperFinished()
	{
		if (!m_OnJumperFinished)
			m_OnJumperFinished = new ScriptInvoker();
		return m_OnJumperFinished;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		if (SCR_Global.IsEditMode())
			return;

		m_aSlots = new array<ref MHJ_AiDropSlot>();
		m_aClientIds = new array<RplId>();
		m_aVisuals = new array<IEntity>();

		if (Replication.IsServer())
			s_Live = this;
	}

	//------------------------------------------------------------------------------------------------
	bool AddJumper(notnull ChimeraCharacter jumper, vector lz)
	{
		if (!Replication.IsServer())
			return false;
		if (ActiveSlotCount() >= MHJ_Constants.AI_DROP_MAX)
		{
			MHJ_Log.Warning("AI drop stick full");
			return false;
		}
		if (FindSlot(jumper))
			return false;

		RplComponent jumperRpl = RplComponent.Cast(jumper.FindComponent(RplComponent));
		if (!jumperRpl)
		{
			MHJ_Log.Warning("AI drop jumper has no RplComponent");
			return false;
		}

		RplId jumperId = jumperRpl.Id();
		if (!jumperId.IsValid())
		{
			MHJ_Log.Warning("AI drop jumper RplId invalid");
			return false;
		}

		ref MHJ_AiDropSlot slot = new MHJ_AiDropSlot();
		MHJ_AiDropAutopilot.InitSlot(slot, jumper, lz);
		slot.m_JumperId = jumperId;
		PrepareJumper(jumper);
		m_aSlots.Insert(slot);
		AddClientId(jumperId);
		RelayAdd(jumperId);
		MHJ_Log.Info("AI drop jumper added id=" + jumperId.ToString());
		return true;
	}

	//------------------------------------------------------------------------------------------------
	bool HasActiveSlots()
	{
		return ActiveSlotCount() > 0;
	}

	//------------------------------------------------------------------------------------------------
	protected int ActiveSlotCount()
	{
		if (!m_aSlots)
			return 0;

		int count = m_aSlots.Count();
		int active = 0;
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_AiDropSlot slot = m_aSlots[i];
			if (slot && slot.m_bActive)
				active = active + 1;
		}
		return active;
	}

	//------------------------------------------------------------------------------------------------
	protected MHJ_AiDropSlot FindSlot(IEntity character)
	{
		if (!m_aSlots || !character)
			return null;

		int count = m_aSlots.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_AiDropSlot slot = m_aSlots[i];
			if (slot && slot.m_bActive && slot.m_Character == character)
				return slot;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (Replication.IsServer())
			TickAuthority(timeSlice);
		TickClientVisuals();
	}

	//------------------------------------------------------------------------------------------------
	protected void TickAuthority(float timeSlice)
	{
		if (!m_aSlots)
			return;

		int count = m_aSlots.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_AiDropSlot slot = m_aSlots[i];
			if (!slot || !slot.m_bActive)
				continue;
			TickSlot(slot, timeSlice);
		}

		if (!m_bClosing && ActiveSlotCount() <= 0 && count > 0)
			BeginClose();
	}

	//------------------------------------------------------------------------------------------------
	protected void TickSlot(notnull MHJ_AiDropSlot slot, float dt)
	{
		ChimeraCharacter jumper = slot.m_Character;
		if (!jumper)
		{
			FinishSlot(slot, null);
			return;
		}

		CharacterControllerComponent controller = jumper.GetCharacterController();
		if (controller && controller.IsDead())
		{
			RestoreJumper(jumper, false);
			FinishSlot(slot, jumper);
			return;
		}

		MHJ_AiDropAutopilot.Step(slot, dt);
		ApplySlotToJumper(slot);

		float agl = MHJ_AiDropAutopilot.GetAgl(slot.m_vOrigin);
		if (agl <= MHJ_Constants.LAND_AGL)
		{
			LandSlot(slot);
			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplySlotToJumper(notnull MHJ_AiDropSlot slot)
	{
		ChimeraCharacter jumper = slot.m_Character;
		if (!jumper)
			return;

		Physics physics = jumper.GetPhysics();
		if (physics)
		{
			physics.EnableGravity(false);
			physics.SetVelocity(vector.Zero);
			physics.SetAngularVelocity(vector.Zero);
		}

		jumper.SetOrigin(slot.m_vOrigin);
		vector ypr;
		ypr[0] = slot.m_fHeading * Math.RAD2DEG;
		ypr[1] = 0;
		ypr[2] = 0;
		jumper.SetYawPitchRoll(ypr);
	}

	//------------------------------------------------------------------------------------------------
	protected void LandSlot(notnull MHJ_AiDropSlot slot)
	{
		ChimeraCharacter jumper = slot.m_Character;
		vector origin = slot.m_vOrigin;
		BaseWorld world = GetGame().GetWorld();
		if (world)
			origin[1] = world.GetSurfaceY(origin[0], origin[2]) + 0.1;
		slot.m_vOrigin = origin;
		slot.m_vVel = vector.Zero;

		if (jumper)
		{
			jumper.SetOrigin(origin);
			RestoreJumper(jumper, true);
		}

		MHJ_Log.Info("AI drop landed id=" + slot.m_JumperId.ToString());
		FinishSlot(slot, jumper);
	}

	//------------------------------------------------------------------------------------------------
	protected void PrepareJumper(notnull ChimeraCharacter jumper)
	{
		Physics physics = jumper.GetPhysics();
		if (physics)
			physics.EnableGravity(false);

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(jumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_BeginAiDropIgnore();
	}

	//------------------------------------------------------------------------------------------------
	protected void RestoreJumper(notnull ChimeraCharacter jumper, bool landed)
	{
		Physics physics = jumper.GetPhysics();
		if (physics)
		{
			physics.EnableGravity(true);
			physics.SetVelocity(vector.Zero);
			physics.SetAngularVelocity(vector.Zero);
		}

		SCR_CharacterCommandHandlerComponent handler = SCR_CharacterCommandHandlerComponent.Cast(jumper.FindComponent(SCR_CharacterCommandHandlerComponent));
		if (handler)
			handler.MHJ_BeginAiDropIgnore();
	}

	//------------------------------------------------------------------------------------------------
	protected void FinishSlot(notnull MHJ_AiDropSlot slot, IEntity jumper)
	{
		if (!slot.m_bActive)
			return;

		slot.m_bActive = false;
		RplId jumperId = slot.m_JumperId;
		RemoveClientId(jumperId);
		RelayRemove(jumperId);

		if (m_OnJumperFinished && jumper)
			m_OnJumperFinished.Invoke(jumper);
	}

	//------------------------------------------------------------------------------------------------
	protected void BeginClose()
	{
		if (m_bClosing)
			return;
		m_bClosing = true;
		GetGame().GetCallqueue().CallLater(DeleteStick, 400, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void DeleteStick()
	{
		if (!Replication.IsServer())
			return;
		if (s_Live == this)
			s_Live = null;

		ClearClientVisuals();
		SCR_EntityHelper.DeleteEntityAndChildren(this);
	}

	//------------------------------------------------------------------------------------------------
	protected void RelayAdd(RplId jumperId)
	{
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
			gameMode.MHJ_RelayAiDropAdd(jumperId);
	}

	//------------------------------------------------------------------------------------------------
	protected void RelayRemove(RplId jumperId)
	{
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
			gameMode.MHJ_RelayAiDropRemove(jumperId);
	}

	//------------------------------------------------------------------------------------------------
	protected void AddClientId(RplId jumperId)
	{
		if (!jumperId.IsValid())
			return;
		if (!m_aClientIds)
			m_aClientIds = new array<RplId>();
		if (FindClientId(jumperId) >= 0)
			return;
		m_aClientIds.Insert(jumperId);
		if (!m_aVisuals)
			m_aVisuals = new array<IEntity>();
		m_aVisuals.Insert(null);
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveClientId(RplId jumperId)
	{
		if (!m_aClientIds)
			return;

		int index = FindClientId(jumperId);
		if (index < 0)
			return;

		DestroyVisualAt(index);
		m_aClientIds.Remove(index);
		if (m_aVisuals && index < m_aVisuals.Count())
			m_aVisuals.Remove(index);
	}

	//------------------------------------------------------------------------------------------------
	protected int FindClientId(RplId jumperId)
	{
		if (!m_aClientIds)
			return -1;

		int count = m_aClientIds.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			if (m_aClientIds[i] == jumperId)
				return i;
		}
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	protected void TickClientVisuals()
	{
		DriveVisualList(m_aClientIds, m_aVisuals);
	}

	//------------------------------------------------------------------------------------------------
	static void ClientAddRemote(RplId jumperId)
	{
		if (!jumperId.IsValid())
			return;
		if (s_Live)
			return;
		if (!s_aRemoteIds)
			s_aRemoteIds = new array<RplId>();
		if (!s_aRemoteVisuals)
			s_aRemoteVisuals = new array<IEntity>();

		int count = s_aRemoteIds.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			if (s_aRemoteIds[i] == jumperId)
				return;
		}

		s_aRemoteIds.Insert(jumperId);
		s_aRemoteVisuals.Insert(null);
		if (s_bRemoteTick)
			return;

		s_bRemoteTick = true;
		GetGame().GetCallqueue().CallLater(TickRemoteVisuals, 0, true);
	}

	//------------------------------------------------------------------------------------------------
	static void ClientRemoveRemote(RplId jumperId)
	{
		if (!s_aRemoteIds)
			return;

		int count = s_aRemoteIds.Count();
		int i;
		int index = -1;
		for (i = 0; i < count; i++)
		{
			if (s_aRemoteIds[i] == jumperId)
			{
				index = i;
				break;
			}
		}
		if (index < 0)
			return;

		if (s_aRemoteVisuals && index < s_aRemoteVisuals.Count())
		{
			IEntity visual = s_aRemoteVisuals[index];
			if (visual)
				SCR_EntityHelper.DeleteEntityAndChildren(visual);
			s_aRemoteVisuals.Remove(index);
		}
		s_aRemoteIds.Remove(index);
	}

	//------------------------------------------------------------------------------------------------
	protected static void TickRemoteVisuals()
	{
		if (s_Live)
			return;
		if (!s_aRemoteIds || s_aRemoteIds.IsEmpty())
		{
			if (s_bRemoteTick)
			{
				GetGame().GetCallqueue().Remove(TickRemoteVisuals);
				s_bRemoteTick = false;
			}
			return;
		}

		DriveVisualList(s_aRemoteIds, s_aRemoteVisuals);
	}

	//------------------------------------------------------------------------------------------------
	protected static void DriveVisualList(array<RplId> ids, array<IEntity> visuals)
	{
		if (!ids)
			return;

		int count = ids.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			IEntity jumper = ResolveJumper(ids[i]);
			if (!jumper)
				continue;

			IEntity visual = null;
			if (visuals && i < visuals.Count())
				visual = visuals[i];
			if (!visual)
			{
				visual = SpawnVisual();
				if (visuals && i < visuals.Count())
					visuals[i] = visual;
			}
			if (!visual)
				continue;

			DriveVisual(visual, jumper);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static IEntity SpawnVisual()
	{
		Resource resource = Resource.Load(MHJ_Constants.CANOPY_MESH);
		if (!resource || !resource.IsValid())
			return null;

		BaseResourceObject baseResource = resource.GetResource();
		if (!baseResource)
			return null;

		VObject mesh = baseResource.ToVObject();
		if (!mesh)
			return null;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return null;

		ref EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		IEntity visual = GetGame().SpawnEntity(GenericEntity, world, spawnParams);
		if (!visual)
			return null;

		visual.SetObject(mesh, BuildVisibleCanopyRemap(mesh));
		visual.SetFlags(EntityFlags.VISIBLE, true);
		return visual;
	}

	//------------------------------------------------------------------------------------------------
	//! The raw xob only has Chute/Line. 01__Default is a prefab MeshObject slot
	//! on MHJ_DeployedCanopy, not a source material on the mesh. Remapping it
	//! here is `Source material do not exist`.
	protected static string BuildVisibleCanopyRemap(notnull VObject mesh)
	{
		string remap;
		string materials[256];
		int numMats = mesh.GetMaterials(materials);
		int i;
		for (i = 0; i < numMats; i++)
		{
			if (materials[i] == "Chute")
				remap = remap + "$remap 'Chute' '" + MHJ_Constants.CANOPY_MAT_CHUTE + "'; ";
			else if (materials[i] == "Line")
				remap = remap + "$remap 'Line' '" + MHJ_Constants.CANOPY_MAT_LINE + "'; ";
		}
		return remap;
	}

	//------------------------------------------------------------------------------------------------
	protected static void DriveVisual(notnull IEntity visual, notnull IEntity jumper)
	{
		vector mat[4];
		jumper.GetWorldTransform(mat);
		vector origin = mat[3];
		origin[1] = origin[1] + MHJ_Constants.CANOPY_AI_VISUAL_Y;
		mat[3] = origin;
		visual.SetWorldTransform(mat);
		visual.Update();

		if (s_bLoggedVisualY)
			return;
		s_bLoggedVisualY = true;
		MHJ_Log.Info("AI canopy visual Y=" + MHJ_Constants.CANOPY_AI_VISUAL_Y.ToString());
	}

	//------------------------------------------------------------------------------------------------
	protected static IEntity ResolveJumper(RplId jumperId)
	{
		if (!jumperId.IsValid())
			return null;

		Managed instance = Replication.FindItem(jumperId);
		if (!instance)
			return null;

		RplComponent replication = RplComponent.Cast(instance);
		if (replication)
			return replication.GetEntity();
		return IEntity.Cast(instance);
	}

	//------------------------------------------------------------------------------------------------
	protected void DestroyVisualAt(int index)
	{
		if (!m_aVisuals || index < 0 || index >= m_aVisuals.Count())
			return;

		IEntity visual = m_aVisuals[index];
		if (visual)
			SCR_EntityHelper.DeleteEntityAndChildren(visual);
		m_aVisuals[index] = null;
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearClientVisuals()
	{
		if (!m_aVisuals)
			return;

		int count = m_aVisuals.Count();
		int i;
		for (i = 0; i < count; i++)
			DestroyVisualAt(i);
		m_aVisuals.Clear();
	}
}
