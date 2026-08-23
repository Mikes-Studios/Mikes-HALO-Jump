//------------------------------------------------------------------------------------------------
//! Server-authoritative AI drop stick. One replicated entity drives every jumper
//! with autopilot. Clients spawn local-only canopy meshes from replicated RplIds.
//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "MHJ", description: "AI canopy drop director. One stick, many jumpers.")]
class MHJ_AiDropDirectorClass : ScriptComponentClass
{
}

class MHJ_AiDropDirector : ScriptComponent
{
	protected static MHJ_AiDropDirector s_Live;

	protected RplComponent m_Rpl;
	protected ref array<ref MHJ_AiDropSlot> m_aSlots;
	protected ref array<RplId> m_aClientIds;
	protected ref array<IEntity> m_aVisuals;
	protected ref ScriptInvoker m_OnJumperFinished;
	protected bool m_bClosing;

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

		Resource resource = Resource.Load(MHJ_Constants.AI_DROP_STICK_PREFAB);
		if (!resource || !resource.IsValid())
		{
			MHJ_Log.Error("AI drop stick prefab missing");
			return null;
		}

		ref EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		spawnParams.Transform[3] = lz;

		IEntity stick = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawnParams);
		if (!stick)
			return null;

		MHJ_AiDropDirector director = MHJ_AiDropDirector.Cast(stick.FindComponent(MHJ_AiDropDirector));
		if (!director)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(stick);
			return null;
		}

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
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (SCR_Global.IsEditMode())
			return;

		m_Rpl = RplComponent.Cast(owner.FindComponent(RplComponent));
		m_aSlots = new array<ref MHJ_AiDropSlot>();
		m_aClientIds = new array<RplId>();
		m_aVisuals = new array<IEntity>();
		SetEventMask(owner, EntityEvent.FRAME | EntityEvent.SIMULATE);

		if (IsAuthority())
			s_Live = this;
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		ClearClientVisuals();
		if (s_Live == this)
			s_Live = null;
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	override bool RplSave(ScriptBitWriter writer)
	{
		int count = m_aSlots.Count();
		int active = 0;
		int i;
		for (i = 0; i < count; i++)
		{
			MHJ_AiDropSlot slot = m_aSlots[i];
			if (slot && slot.m_bActive && slot.m_JumperId.IsValid())
				active = active + 1;
		}

		writer.WriteInt(active);
		for (i = 0; i < count; i++)
		{
			MHJ_AiDropSlot slot = m_aSlots[i];
			if (!slot || !slot.m_bActive || !slot.m_JumperId.IsValid())
				continue;
			writer.WriteRplId(slot.m_JumperId);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool RplLoad(ScriptBitReader reader)
	{
		int count;
		reader.ReadInt(count);
		if (count < 0)
			count = 0;
		if (count > MHJ_Constants.AI_DROP_MAX)
			count = MHJ_Constants.AI_DROP_MAX;

		int i;
		for (i = 0; i < count; i++)
		{
			RplId jumperId;
			reader.ReadRplId(jumperId);
			AddClientId(jumperId);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	bool AddJumper(notnull ChimeraCharacter jumper, vector lz)
	{
		if (!IsAuthority())
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
		Rpc(RpcDo_MHJ_AiDropAdd, jumperId);
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
	override void EOnSimulate(IEntity owner, float timeSlice)
	{
		if (!IsAuthority())
			return;
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
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		TickClientVisuals();
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
		Rpc(RpcDo_MHJ_AiDropRemove, jumperId);

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
		if (!IsAuthority())
			return;
		if (s_Live == this)
			s_Live = null;

		IEntity owner = GetOwner();
		if (owner)
			RplComponent.DeleteRplEntity(owner, false);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_MHJ_AiDropAdd(RplId jumperId)
	{
		AddClientId(jumperId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_MHJ_AiDropRemove(RplId jumperId)
	{
		RemoveClientId(jumperId);
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
		if (Replication.IsServer() && !GetGame().GetPlayerController())
			return;
		if (!m_aClientIds)
			return;

		int count = m_aClientIds.Count();
		int i;
		for (i = 0; i < count; i++)
		{
			IEntity jumper = ResolveJumper(m_aClientIds[i]);
			if (!jumper)
				continue;

			IEntity visual = null;
			if (m_aVisuals && i < m_aVisuals.Count())
				visual = m_aVisuals[i];
			if (!visual)
			{
				visual = SpawnVisual();
				if (m_aVisuals && i < m_aVisuals.Count())
					m_aVisuals[i] = visual;
			}
			if (!visual)
				continue;

			DriveVisual(visual, jumper);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity SpawnVisual()
	{
		Resource resource = Resource.Load(MHJ_Constants.AI_CANOPY_VISUAL_PREFAB);
		if (!resource || !resource.IsValid())
			return null;

		ref EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		return GetGame().SpawnEntityPrefabLocal(resource, GetGame().GetWorld(), spawnParams);
	}

	//------------------------------------------------------------------------------------------------
	protected void DriveVisual(notnull IEntity visual, notnull IEntity jumper)
	{
		vector mat[4];
		jumper.GetWorldTransform(mat);
		vector origin = mat[3];
		origin[1] = origin[1] + MHJ_Constants.CANOPY_HANG_LENGTH;
		mat[3] = origin;
		visual.SetWorldTransform(mat);
		visual.Update();
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity ResolveJumper(RplId jumperId)
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

	//------------------------------------------------------------------------------------------------
	protected bool IsAuthority()
	{
		if (!m_Rpl)
			return Replication.IsServer();
		return m_Rpl.Role() == RplRole.Authority;
	}
}
