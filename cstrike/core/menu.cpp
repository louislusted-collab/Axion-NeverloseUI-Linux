#include "menu.h"

// used: config variables
#include "variables.h"
// used: entity stuff for skinchanger etc
#include "../cstrike/sdk/entity.h"
// used: iinputsystem
#include "interfaces.h"
#include "../sdk/interfaces/iengineclient.h"
#include "../sdk/interfaces/inetworkclientservice.h"
#include "../sdk/interfaces/iglobalvars.h"
#include "../sdk/interfaces/ienginecvar.h"
// used: overlay's context
#include "../features/visuals/overlay.h"
// used: notifications
#include "../utilities/notify.h"
#include "gui.hpp"
#ifdef _WIN32
#include <d3d11.h>
#endif
#ifdef _WIN32
#include <d3dcompiler.h>
#endif
#include "../cstrike/features/skins/ccsplayerinventory.hpp"
#include "../cstrike/features/skins/ccsinventorymanager.hpp"
#include "../cstrike/features/skins/skin_changer.hpp"
#include "imgui/imgui_edited.hpp"
#include <cstring>
#ifdef __linux__
#include "../../linux/vulkan_hook.h"
#include "../../linux/native_esp.h"
#include "../../linux/native_chams.h"
#include "../sdk/interfaces/itrace.h"
#endif
#pragma region menu_array_entries
static void RenderInventoryWindow();
int page = 0;
float tab_alpha = 0.f;
float tab_add = 0.f;
int active_tab = 0;
static constexpr const char* arrMiscDpiScale[] = {
	"100%",
	"125%",
	"150%",
	"175%",
	"200%"
};
int subtab;
static const std::pair<const char*, const std::size_t> arrColors[] = {
	{ "[accent] - main", Vars.colAccent0 },
	{ "[accent] - dark (hover)", Vars.colAccent1 },
	{ "[accent] - darker (active)", Vars.colAccent2 },
	{ "[primitive] - text", Vars.colPrimtv0 },
	{ "[primitive] - background", Vars.colPrimtv1 },
	{ "[primitive] - disabled", Vars.colPrimtv2 },
	{ "[primitive] - frame background", Vars.colPrimtv3 },
	{ "[primitive] - border", Vars.colPrimtv4 },
};

static constexpr const char* arrMenuAddition[] = {
	"dim",
	"particle",
	"glow"
};
static constexpr const char* arrEspFlags[] = {
	"Armor",
	"KIT",
	"Scoped",
	"Flashed",
	"Defusing",
	"Planting",
	"Reloading",
	"Bomb carrier",
	"Ping"
};
static constexpr const char* arrLegitCond[] = {
	"In air",
	"Flashed",
	"Thru smoke",
	"Delay on kill"
};
static constexpr const char* arrTriggerHitboxes[] = {
	"Head",
	"Torso",
	"Arms",
	"Legs"
};
static constexpr const char* arrMovementStrafer[] = {
	"Adjust mouse",
	"Directional"
};

enum TAB : int {
	rage = 0,
	legit = 1,
	visuals = 2,
	misc = 3,
	skinchanger = 4,
	cloud = 5,
	scripting = 6,
};

enum SUBTAB : int {
	first = 0,
	seccond = 1,
	third = 4,
	fifth = 5,
};

// Function to extract the unique identifier from the itemBaseName
std::string ExtractIdentifier(const std::string& itemBaseName, const std::string& modelName) {
	// Find the position of the modelName
	size_t modelPos = itemBaseName.find(modelName);

	// If modelName is found, extract the substring after it
	if (modelPos != std::string::npos) {
		// Find the next "/"
		size_t nextSlashPos = itemBaseName.find("/", modelPos + modelName.length());

		// Extract the substring after modelName until the next "/"
		return itemBaseName.substr(modelPos + modelName.length(), nextSlashPos - (modelPos + modelName.length()));
	}

	// If modelName is not found, return an empty string
	return "";
}
ImTextureID CreateTextureFromMemory([[maybe_unused]] void* imageData, [[maybe_unused]] int width, [[maybe_unused]] int height) {
#ifdef _WIN32
	ID3D11Texture2D* pTexture = nullptr;
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width; desc.Height = height;
	desc.MipLevels = 1; desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = imageData; initData.SysMemPitch = width * 4;
	if (FAILED(I::Device->CreateTexture2D(&desc, &initData, &pTexture))) return 0;
	return (ImTextureID)pTexture;
#else
	return nullptr; // Vulkan texture upload handled separately
#endif
}
enum wep_type : int {
	PISTOL = 1,
	 HEAVY_PISTOL = 2,
	 ASSULT = 3,
	 SNIPERS = 4,
	 SCOUT = 5, 
	 AWP =6,
};
void MENU::RenderMainWindow()
{
	static constexpr float windowWidth = 540.f;

	struct DumpedSkin_t {
		std::string m_name = "";
		int m_ID = 0;
		int m_rarity = 0;
	};
	struct DumpedItem_t {
		std::string m_name = "";
		uint16_t m_defIdx = 0;
		void* m_image = nullptr;
		ImTextureID m_textureID = nullptr;
		int m_rarity = 0;
		bool m_unusualItem = false;
		std::vector<DumpedSkin_t> m_dumpedSkins{};
		DumpedSkin_t* pSelectedSkin = nullptr;
	};
	static std::vector<DumpedItem_t> vecDumpedItems;
	static DumpedItem_t* pSelectedItem = nullptr;

	CEconItemSchema* pItemSchema = nullptr;
#ifdef _WIN32
	pItemSchema = I::Client->GetEconItemSystem()->GetEconItemSchema();
#endif


	// Render the ImGui draw data using the DirectX 11 blur shader
	//blurShader.Render(drawData);
	ImGuiIO& io = ImGui::GetIO();
	ImGuiStyle& style = ImGui::GetStyle();

	// @test: we should always update the animation?
	animMenuDimBackground.Update(io.DeltaTime, style.AnimationSpeed);
	if (!bMainWindowOpened)
		return;

	const ImVec2 vecScreenSize = io.DisplaySize;
	const float flBackgroundAlpha = animMenuDimBackground.GetValue(1.f);
	flDpiScale = 1.50f;

	// @note: we call this every frame because we utilizing rainbow color as well! however it's not really performance friendly?
	UpdateStyle(&style);

	if (flBackgroundAlpha > 0.f)
	{
	#ifdef _WIN32
		if (C_GET(unsigned int, Vars.bMenuAdditional) & MENU_ADDITION_DIM_BACKGROUND)
			D::AddDrawListRect(ImGui::GetBackgroundDrawList(), ImVec2(0, 0), vecScreenSize, C_GET(ColorPickerVar_t, Vars.colPrimtv1).colValue.Set<COLOR_A>(125 * flBackgroundAlpha), DRAW_RECT_FILLED);
	#endif

		if (C_GET(unsigned int, Vars.bMenuAdditional) & MENU_ADDITION_BACKGROUND_PARTICLE)
			menuParticle.Render(ImGui::GetBackgroundDrawList(), vecScreenSize, flBackgroundAlpha);
	}

	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, flBackgroundAlpha);


	style.WindowPadding = ImVec2(0, 0);
	style.ItemSpacing = ImVec2(10 * dpi, 10 * dpi);
	style.WindowBorderSize = 0;
	style.ScrollbarSize = 3.f * dpi;
	Color_t color = Color_t(235, 94, 52, 255 );
	c::accent = color.GetVec4(1.f) ;

	ImGui::SetNextWindowSize(c::background::size* dpi);
	ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_Once, ImVec2(0.5f, 0.5f));


	// render main window
	if (ImGui::Begin(CS_XOR("handle0000"), &bMainWindowOpened, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
	


		const ImVec2& pos = ImGui::GetWindowPos();
		const ImVec2& region = ImGui::GetContentRegionMax();
		const ImVec2& spacing = style.ItemSpacing;

		ImGui::GetBackgroundDrawList()->AddRectFilled(pos, pos + c::background::size * dpi, ImGui::GetColorU32(c::background::filling), c::background::rounding);
		ImGui::GetBackgroundDrawList()->AddRectFilled(pos, pos + ImVec2(200.f * dpi, c::background::size.y * dpi), ImGui::GetColorU32(c::tab::border), c::background::rounding, ImDrawFlags_RoundCornersLeft);
		ImGui::GetBackgroundDrawList()->AddLine(pos + ImVec2(200.f * dpi, 0.f), pos + ImVec2(200.f, c::background::size.y * dpi), ImGui::GetColorU32(c::background::stroke), 1.f);

		ImGui::GetBackgroundDrawList()->AddRect(pos, pos + c::background::size * dpi, ImGui::GetColorU32(c::background::stroke), c::background::rounding);

		ImGui::SetCursorPos({ 5, 10 });
		ImGui::BeginGroup();
		{
			std::vector<std::vector<std::string>> tab_columns = {
				{ "c", "a", "b", "d", "f", "o", "e" },
				{ "Ragebot", "Legitbot", "Antiaim", "Removals", "Visuals", "Skins", "Misc" },
				{ "Aggressive target assistance...", "Subtle aim assistance...", "Angle and state controls...", "Remove obstructive effects...", "Visualization", "Item customization...", "Runtime, movement, and configs..." },
				{ "", "", "", "", "", "", "" }
			};

			const int num_tabs = tab_columns[0].size();

			for (int i = 0; i < num_tabs; ++i)
				if (edited::Tab(page == i, tab_columns[0][i].c_str(), tab_columns[1][i].c_str(), tab_columns[2][i].c_str(), ImVec2(180, 50))) {
					page = i;

					//notificationSystem.AddNotification(tab_columns[3][i], 1000);
				}
		}
		ImGui::EndGroup();

		tab_alpha = ImLerp(tab_alpha, (page == active_tab) ? 1.f : 0.f, 15.f * ImGui::GetIO().DeltaTime);
		if (tab_alpha < 0.01f && tab_add < 0.01f) active_tab = page;

		ImGui::SetCursorPos(ImVec2(200, 100 - (tab_alpha * 100)));
		int& rageWeaponSelection = C_GET(int, Vars.rage_weapon_selection);
		rageWeaponSelection = std::clamp(rageWeaponSelection, 0, 6);
		const int current_weapon = rageWeaponSelection;

		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tab_alpha * style.Alpha);
		{
			if (active_tab == 0)
			{
				edited::BeginChild(CS_XOR("##Container0"), ImVec2((c::background::size.x - 200) / 2, c::background::size.y), 0);
				{
					ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), CS_XOR("Weapons"));
					const char* weapons[7]{ CS_XOR("Default"), CS_XOR("Pistols"), CS_XOR("Heavy Pistols"),CS_XOR("Assault Rifles"), CS_XOR("Auto Snipers"),CS_XOR("Scout"), CS_XOR("AWP") };
					edited::Combo(CS_XOR("Weapon"), CS_XOR("Select weapon for current configuration"), &C_GET(int, Vars.rage_weapon_selection), weapons, IM_ARRAYSIZE(weapons), 6);
					// run rage cfg depending on weapon
					{
						ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "General");
							edited::Checkbox("Enabled", "Activate ragebot", &C_GET(bool, Vars.rage_enable));
							edited::Checkbox("Auto shoot", "Fire through the native command path when every selected condition passes", &C_GET(bool, Vars.rage_auto_shoot));
							edited::Checkbox("Hitscan", "Evaluate every enabled hitbox and use the best live point", &C_GET(bool, Vars.rage_hitscan));

						int& targetSelection = C_GET_ARRAY(int, 7, Vars.rage_target_select, current_weapon);
						targetSelection = std::clamp(targetSelection, 0, 2);
						const char* targets_select[3]{ CS_XOR("Distance"), CS_XOR("Damage"),CS_XOR("Crosshair") };
						edited::Combo(CS_XOR("Target selection"), CS_XOR("Select target based on conditions"), &C_GET_ARRAY(int, 7, Vars.rage_target_select, current_weapon), targets_select, IM_ARRAYSIZE(targets_select), 6);
						const char* hitbox_priorities[4]{ CS_XOR("Automatic"), CS_XOR("Head / neck"), CS_XOR("Torso"), CS_XOR("Limbs") };
						int& hitboxPriority = C_GET_ARRAY(int, 7, Vars.rage_hitbox_priority, current_weapon);
						hitboxPriority = std::clamp(hitboxPriority, 0, 3);
						edited::Combo(CS_XOR("Hitbox priority"), CS_XOR("Prefer one enabled hitbox group before normal scoring"), &hitboxPriority, hitbox_priorities, IM_ARRAYSIZE(hitbox_priorities), 6);

						if (current_weapon > 3)
							edited::Checkbox("Auto scope", "Scope a sniper after a valid target is acquired", &C_GET_ARRAY(bool, 7, Vars.rage_auto_scope, current_weapon));

						#ifdef _WIN32
						edited::Checkbox("Rapid fire", "Allows you to fire multiple bullets ignoring fire rate", &C_GET_ARRAY(bool, 7, Vars.rapid_fire, current_weapon));
						#endif

							edited::Checkbox("Auto stop", "Stops local player in order to maintain best accuracy", &C_GET_ARRAY(bool, 7, Vars.rage_auto_stop, current_weapon));
							edited::Checkbox("Auto crouch", "Crouch while committing a valid shot", &C_GET_ARRAY(bool, 7, Vars.rage_auto_crouch, current_weapon));
							edited::Checkbox("Multipoint", "Scan center and scaled side points on each selected hitbox", &C_GET_ARRAY(bool, 7, Vars.rage_multipoint, current_weapon));
							if (C_GET_ARRAY(bool, 7, Vars.rage_multipoint, current_weapon))
								edited::SliderFloat("Multipoint scale", "Percentage of the approximate hitbox radius", &C_GET_ARRAY(float, 7, Vars.rage_multipoint_scale, current_weapon), 10.f, 100.f, "%.0f%%");

						ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Accuracy");
						#ifdef __linux__
						ImGui::TextWrapped("Native ballistics uses live weapon, armor, surface and trace data. A client-build or schema mismatch holds the shot and reports the unavailable condition.");
						#endif

						edited::Checkbox("Hitchance", "Allows you to hit players with more accuracy", &C_GET_ARRAY(bool, 7, Vars.rage_hitchance, current_weapon));
						if (C_GET_ARRAY(bool, 7, Vars.rage_hitchance, current_weapon))
						{
							ImGui::SliderInt(CS_XOR("chance"), &C_GET_ARRAY(int, 7, Vars.rage_minimum_hitchance, current_weapon), 0, 100);
						}

							edited::Checkbox("Penetration", "Allows you to hit players through objects", &C_GET_ARRAY(bool, 7, Vars.rage_penetration, current_weapon));
							ImGui::SliderInt(CS_XOR("Minimum damage"), &C_GET_ARRAY(int, 7, Vars.rage_minimum_damage, current_weapon), 0, 130);
							edited::Checkbox("Damage preview", "Draw estimated penetration damage beside the selected target", &C_GET_ARRAY(bool, 7, Vars.rage_damage_preview, current_weapon));

							ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Preferences");
							edited::Checkbox("Lethal body", "Prefer a body point when its estimated damage is lethal", &C_GET_ARRAY(bool, 7, Vars.rage_lethal_body, current_weapon));
							edited::Checkbox("Prefer exposed", "Score visible points ahead of penetrated points", &C_GET_ARRAY(bool, 7, Vars.rage_prefer_exposed, current_weapon));
							edited::Checkbox("Prefer low health", "Prioritize enemies with less remaining health", &C_GET_ARRAY(bool, 7, Vars.rage_prefer_low_health, current_weapon));
							edited::Checkbox("Prefer damage", "Prioritize the point with the highest estimated damage", &C_GET_ARRAY(bool, 7, Vars.rage_prefer_high_damage, current_weapon));
							edited::Checkbox("Delay until accurate", "Wait until the configured hitchance is reached", &C_GET_ARRAY(bool, 7, Vars.rage_delay_accurate, current_weapon));
							edited::Checkbox("Delay until visible", "Do not fire a penetrated point until it becomes exposed", &C_GET_ARRAY(bool, 7, Vars.rage_delay_visible, current_weapon));
							edited::Keybind("Force body", "Hold to restrict scanning to torso and pelvis", &C_GET(int, Vars.rage_force_body_key));
							edited::Keybind("Force head", "Hold to restrict scanning to the head", &C_GET(int, Vars.rage_force_head_key));
							edited::Checkbox("Decision overlay", "Show target, damage, hitchance and shot decision", &C_GET(bool, Vars.rage_decision_overlay));
							edited::Checkbox("Shot logging", "Write every fire/hold reason to the native diagnostics log", &C_GET(bool, Vars.rage_shot_logging));
					}
				}
				edited::EndChild();
				ImGui::SameLine(0, 0);

				edited::BeginChild("##Container1", ImVec2((c::background::size.x - 200 * dpi) / 2, c::background::size.y), 0);
				{
					ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Hitbox System");

					/* render model preview*/
					ImGui::SetCursorPos({ 55, 75 });
					#ifdef _WIN32
					ImGui::Image((void*)I::Maintexture, ImVec2(278, 380));
					#else
					if (g_PreviewTexture != nullptr)
						ImGui::Image((ImTextureID)g_PreviewTexture, ImVec2(278, 380));
					else
						ImGui::Dummy(ImVec2(278, 380));
					#endif
					switch (current_weapon) {
					case PISTOL:
						edited::pointbox(CS_XOR("##head"), &C_GET_ARRAY(bool, 7, Vars.hitbox_head, 1), 0, 115.f, 105.f);
						edited::pointbox(CS_XOR("##chest"), &C_GET_ARRAY(bool, 7, Vars.hitbox_uppeer_chest, 1), 0, 130.f, 160.f);
						edited::pointbox(CS_XOR("##stomach"), &C_GET_ARRAY(bool, 7, Vars.hitbox_stomach, 1), 0, 130.f, 230.f);
						edited::pointbox(CS_XOR("##leg_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 1), 0, 110.f, 320);
						edited::pointbox(CS_XOR("##leg_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 1), 0, 170.f, 320);
						edited::pointbox(CS_XOR("##feet_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 1), 0, 120.f, 400);
						edited::pointbox(CS_XOR("##feet_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 1), 0, 175.f, 400);
						break;
					case HEAVY_PISTOL:
						edited::pointbox(CS_XOR("##head"), &C_GET_ARRAY(bool, 7, Vars.hitbox_head, 2), 0, 115.f, 105.f);
						edited::pointbox(CS_XOR("##chest"), &C_GET_ARRAY(bool, 7, Vars.hitbox_uppeer_chest, 2), 0, 130.f, 160.f);
						edited::pointbox(CS_XOR("##stomach"), &C_GET_ARRAY(bool, 7, Vars.hitbox_stomach, 2), 0, 130.f, 230.f);
						edited::pointbox(CS_XOR("##leg_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 2), 0, 110.f, 320);
						edited::pointbox(CS_XOR("##leg_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 2), 0, 170.f, 320);
						edited::pointbox(CS_XOR("##feet_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 2), 0, 120.f, 400);
						edited::pointbox(CS_XOR("##feet_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 2), 0, 175.f, 400);
						break;
					case ASSULT:
						edited::pointbox(CS_XOR("##head"), &C_GET_ARRAY(bool, 7, Vars.hitbox_head, 3), 0, 115.f, 105.f);
						edited::pointbox(CS_XOR("##chest"), &C_GET_ARRAY(bool, 7, Vars.hitbox_uppeer_chest, 3), 0, 130.f, 160.f);
						edited::pointbox(CS_XOR("##stomach"), &C_GET_ARRAY(bool, 7, Vars.hitbox_stomach, 3), 0, 130.f, 230.f);
						edited::pointbox(CS_XOR("##leg_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 3), 0, 110.f, 320);
						edited::pointbox(CS_XOR("##leg_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 3), 0, 170.f, 320);
						edited::pointbox(CS_XOR("##feet_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 3), 0, 120.f, 400);
						edited::pointbox(CS_XOR("##feet_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 3), 0, 175.f, 400);
						break;
					case SNIPERS:
						edited::pointbox(CS_XOR("##head"), &C_GET_ARRAY(bool, 7, Vars.hitbox_head, 4), 0, 115.f, 105.f);
						edited::pointbox(CS_XOR("##chest"), &C_GET_ARRAY(bool, 7, Vars.hitbox_uppeer_chest, 4), 0, 130.f, 160.f);
						edited::pointbox(CS_XOR("##stomach"), &C_GET_ARRAY(bool, 7, Vars.hitbox_stomach, 4), 0, 130.f, 230.f);
						edited::pointbox(CS_XOR("##leg_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 4), 0, 110.f, 320);
						edited::pointbox(CS_XOR("##leg_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 4), 0, 170.f, 320);
						edited::pointbox(CS_XOR("##feet_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 4), 0, 120.f, 400);
						edited::pointbox(CS_XOR("##feet_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 4), 0, 175.f, 400);
						break;
					case SCOUT:
						edited::pointbox(CS_XOR("##head"), &C_GET_ARRAY(bool, 7, Vars.hitbox_head, 5), 0, 115.f, 105.f);
						edited::pointbox(CS_XOR("##chest"), &C_GET_ARRAY(bool, 7, Vars.hitbox_uppeer_chest, 5), 0, 130.f, 160.f);
						edited::pointbox(CS_XOR("##stomach"), &C_GET_ARRAY(bool, 7, Vars.hitbox_stomach, 5), 0, 130.f, 230.f);
						edited::pointbox(CS_XOR("##leg_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 5), 0, 110.f, 320);
						edited::pointbox(CS_XOR("##leg_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 5), 0, 170.f, 320);
						edited::pointbox(CS_XOR("##feet_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 5), 0, 120.f, 400);
						edited::pointbox(CS_XOR("##feet_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 5), 0, 175.f, 400);
						break;
					case AWP:
						edited::pointbox(CS_XOR("##head"), &C_GET_ARRAY(bool, 7, Vars.hitbox_head, 6), 0, 115.f, 105.f);
						edited::pointbox(CS_XOR("##chest"), &C_GET_ARRAY(bool, 7, Vars.hitbox_uppeer_chest, 6), 0, 130.f, 160.f);
						edited::pointbox(CS_XOR("##stomach"), &C_GET_ARRAY(bool, 7, Vars.hitbox_stomach, 6), 0, 130.f, 230.f);
						edited::pointbox(CS_XOR("##leg_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 6), 0, 110.f, 320);
						edited::pointbox(CS_XOR("##leg_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_legs, 6), 0, 170.f, 320);
						edited::pointbox(CS_XOR("##feet_l"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 6), 0, 120.f, 400);
						edited::pointbox(CS_XOR("##feet_r"), &C_GET_ARRAY(bool, 7, Vars.hitbox_feet, 6), 0, 175.f, 400);
						break;
					}

				}
				edited::EndChild();

			}
			else if (active_tab == 1)
			{
				edited::BeginChild("##LegitbotContainer", ImVec2(c::background::size.x - 200, c::background::size.y), 0);
				{
					ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Legitbot");
						edited::Checkbox("Enable", "Master switch for every Legitbot feature", &C_GET(bool, Vars.legit_ui_enable));
						edited::Checkbox("Per-weapon profiles", "Use a separate targeting profile for each weapon group", &C_GET(bool, Vars.legit_ui_per_weapon));
						const char* legitProfiles[7]{ "Default", "Pistols", "Heavy Pistols", "Assault Rifles", "Auto Snipers", "Scout", "AWP" };
						if (C_GET(bool, Vars.legit_ui_per_weapon))
							edited::Combo("Edit profile", "The active weapon automatically selects its matching profile", &C_GET(int, Vars.legit_ui_profile_selection), legitProfiles, IM_ARRAYSIZE(legitProfiles), 7);
						const int legitProfile = std::clamp(C_GET(int, Vars.legit_ui_profile_selection), 0, 6);
						edited::Checkbox("Legit Aim", "Smooth aim assistance while firing", &C_GET(bool, Vars.legit_ui_aim));
						edited::Keybind("Aim key", "Click the button, then press mouse 1-5", &C_GET(int, Vars.legit_ui_key));
						edited::Checkbox("Toggle aim key", "Press once for on/off instead of holding", &C_GET(bool, Vars.legit_ui_toggle));
						if (C_GET(bool, Vars.legit_ui_per_weapon))
							edited::SliderFloat("Smoothness (ms)", "Time used to settle onto the target", &C_GET_ARRAY(float, 7, Vars.legit_profile_smoothness, legitProfile), 1.f, 500.f, "%.0f ms");
						else
							edited::SliderFloat("Smoothness (ms)", "Time used to settle onto the target", &C_GET(float, Vars.legit_ui_smoothness), 1.f, 500.f, "%.0f ms");
					edited::SliderFloat("Acceleration (ms)", "Time to ramp from a gentle start to full aim speed; 0 disables the ramp", &C_GET(float, Vars.legit_ui_acceleration_ms), 0.f, 500.f, "%.0f ms");
					edited::SliderFloat("Deceleration zone", "Start slowing this many degrees before the target; 0 disables it", &C_GET(float, Vars.legit_ui_deceleration_degrees), 0.f, 10.f, "%.1f deg");
					edited::Checkbox("Artificial overshoot", "Once per target lock, continue a small bounded distance past the target before recovering", &C_GET(bool, Vars.legit_ui_artificial_overshoot));
					if (C_GET(bool, Vars.legit_ui_artificial_overshoot))
						edited::SliderFloat("Overshoot amount", "Maximum angular distance past the target", &C_GET(float, Vars.legit_ui_overshoot_degrees), 0.05f, 1.50f, "%.2f deg");
					edited::SliderFloat("Recovery (ms)", "Correction time after crossing the target naturally or through artificial overshoot", &C_GET(float, Vars.legit_ui_recovery_ms), 5.f, 250.f, "%.0f ms");
					edited::Checkbox("Draw FoV", "Draw the active aim field of view", &C_GET(bool, Vars.legit_ui_draw_fov));
						if (C_GET(bool, Vars.legit_ui_per_weapon))
							edited::SliderFloat("FoV Size", "Field of view size", &C_GET_ARRAY(float, 7, Vars.legit_profile_fov, legitProfile), 5.f, 60.f, "%.0f°");
						else
							edited::SliderFloat("FoV Size", "Field of view size", &C_GET(float, Vars.legit_ui_fov_size), 5.f, 60.f, "%.0f°");
					edited::Checkbox("Recoil compensation", "Compensate the current camera punch", &C_GET(bool, Vars.legit_ui_recoil));
					edited::Checkbox("Velocity prediction", "Lead moving targets", &C_GET(bool, Vars.legit_ui_prediction));
					if (C_GET(bool, Vars.legit_ui_prediction))
						edited::SliderFloat("Prediction (ms)", "Target lead time", &C_GET(float, Vars.legit_ui_prediction_ms), 0.f, 250.f, "%.0f ms");
					edited::Checkbox("Auto shoot", "Fire when the selected bone is centered", &C_GET(bool, Vars.legit_ui_auto_shoot));
					edited::Checkbox("Target head", "Highest target priority", &C_GET(bool, Vars.legit_ui_bone_head));
					edited::Checkbox("Target torso", "Fallback to upper spine", &C_GET(bool, Vars.legit_ui_bone_torso));
						edited::Checkbox("Target arms", "Fallback to elbows", &C_GET(bool, Vars.legit_ui_bone_arms));
						edited::Checkbox("Target legs", "Fallback to knees", &C_GET(bool, Vars.legit_ui_bone_legs));
						const char* legitTargets[3]{ "Closest to crosshair", "Closest distance", "Lowest health" };
						const char* legitHitboxModes[2]{ "Priority order", "Nearest hitbox" };
						if (C_GET(bool, Vars.legit_ui_per_weapon))
						{
							edited::Combo("Target selection", "How enemies are ranked", &C_GET_ARRAY(int, 7, Vars.legit_profile_target_selection, legitProfile), legitTargets, IM_ARRAYSIZE(legitTargets), 3);
							edited::Combo("Hitbox selection", "Priority follows the toggles above; nearest chooses the smallest angular delta", &C_GET_ARRAY(int, 7, Vars.legit_profile_hitbox_mode, legitProfile), legitHitboxModes, IM_ARRAYSIZE(legitHitboxModes), 2);
							edited::Checkbox("Visibility check", "Ignore points blocked by world geometry", &C_GET_ARRAY(bool, 7, Vars.legit_profile_visibility_check, legitProfile));
							edited::Checkbox("Smoke check", "Reject a target when active smoke intersects the complete eye-to-target segment", &C_GET_ARRAY(bool, 7, Vars.legit_profile_smoke_check, legitProfile));
							edited::Checkbox("Flash check", "Pause aim while flashed", &C_GET_ARRAY(bool, 7, Vars.legit_profile_flash_check, legitProfile));
							edited::SliderFloat("Reaction delay", "Wait after acquiring a new target", &C_GET_ARRAY(float, 7, Vars.legit_profile_reaction_ms, legitProfile), 0.f, 500.f, "%.0f ms");
						}
						else
						{
							edited::Combo("Target selection", "How enemies are ranked", &C_GET(int, Vars.legit_ui_target_selection), legitTargets, IM_ARRAYSIZE(legitTargets), 3);
							edited::Combo("Hitbox selection", "Priority follows the toggles above; nearest chooses the smallest angular delta", &C_GET(int, Vars.legit_ui_hitbox_mode), legitHitboxModes, IM_ARRAYSIZE(legitHitboxModes), 2);
							edited::Checkbox("Visibility check", "Ignore points blocked by world geometry", &C_GET(bool, Vars.legit_ui_visibility_check));
							edited::Checkbox("Smoke check", "Reject a target when active smoke intersects the complete eye-to-target segment", &C_GET(bool, Vars.legit_ui_smoke_check));
							edited::Checkbox("Flash check", "Pause aim while flashed", &C_GET(bool, Vars.legit_ui_flash_check));
							edited::SliderFloat("Reaction delay", "Wait after acquiring a new target", &C_GET(float, Vars.legit_ui_reaction_ms), 0.f, 500.f, "%.0f ms");
						}

						ImGui::Separator();
						ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Triggerbot and recoil");
						edited::Checkbox("Triggerbot", "Fire when an allowed hitbox is under the crosshair", &C_GET(bool, Vars.trigger_ui_enable));
						if (C_GET(bool, Vars.trigger_ui_enable))
						{
							edited::Keybind("Trigger key", "Hold to arm triggerbot", &C_GET(int, Vars.trigger_ui_key));
							edited::SliderFloat("Trigger delay", "Wait before firing a stable target", &C_GET(float, Vars.trigger_ui_delay_ms), 0.f, 500.f, "%.0f ms");
							edited::MultiCombo("Trigger hitboxes", &C_GET(unsigned int, Vars.trigger_ui_hitboxes), arrTriggerHitboxes, CS_ARRAYSIZE(arrTriggerHitboxes));
							edited::Checkbox("Trigger visibility", "Require an exposed target", &C_GET(bool, Vars.trigger_ui_visibility_check));
							edited::Checkbox("Trigger smoke check", "Reject points whose complete eye-to-target segment intersects active smoke", &C_GET(bool, Vars.trigger_ui_smoke_check));
							edited::Checkbox("Scoped only", "Require scope for sniper groups", &C_GET(bool, Vars.trigger_ui_scoped_only));
							edited::Checkbox("Trigger diagnostics", "Log acquisition, delay and command decisions", &C_GET(bool, Vars.trigger_ui_diagnostics));
						}
						edited::Checkbox("Standalone recoil control", "Compensate punch while firing even without an aim target", &C_GET(bool, Vars.recoil_ui_enable));
						if (C_GET(bool, Vars.recoil_ui_enable))
							edited::SliderFloat("Recoil smoothing", "Time used to settle the recoil correction", &C_GET(float, Vars.recoil_ui_smoothing_ms), 1.f, 300.f, "%.0f ms");
				}
				edited::EndChild();
			}
			else if (active_tab == 2)
			{
					edited::BeginChild("##Container0", ImVec2(c::background::size.x - 200, c::background::size.y), 0);
					{
						ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Antiaim");
						edited::Checkbox(CS_XOR("Enable"), CS_XOR("Enables Antiaim"), &C_GET(bool, Vars.bAntiAim));
						const char* PitchTypes[5]{ CS_XOR("Off"), CS_XOR("Down"),CS_XOR("Up"), CS_XOR("Zero"), CS_XOR("Custom")};
						edited::Combo(CS_XOR("Pitch"), CS_XOR("Pitch Type"), &C_GET(int, Vars.iPitchType), PitchTypes, IM_ARRAYSIZE(PitchTypes), 5);
						if (C_GET(int, Vars.iPitchType) == 4)
							edited::SliderFloat("Custom pitch", "Exact pitch angle", &C_GET(float, Vars.antiaim_custom_pitch), -89.f, 89.f, "%.0f°");

						const char* BaseYawTypes[5]{ CS_XOR("Off"), CS_XOR("Backwards"),CS_XOR("Forwards"), CS_XOR("Sideways"), CS_XOR("Custom") };
						edited::Combo(CS_XOR("Base Yaw"), CS_XOR("Yaw direction"), &C_GET(int, Vars.iBaseYawType), BaseYawTypes, IM_ARRAYSIZE(BaseYawTypes), 5);
						if (C_GET(int, Vars.iBaseYawType) == 4)
							edited::SliderFloat("Custom yaw", "Offset from the current view yaw", &C_GET(float, Vars.antiaim_custom_yaw), -180.f, 180.f, "%.0f°");

						const char* jitterModes[3]{ "Off", "Static", "Random" };
						edited::Combo("Jitter", "Alternate or randomize around the base yaw", &C_GET(int, Vars.antiaim_jitter_mode), jitterModes, IM_ARRAYSIZE(jitterModes), 3);
						if (C_GET(int, Vars.antiaim_jitter_mode) != 0)
							edited::SliderFloat("Jitter amount", "Maximum yaw displacement", &C_GET(float, Vars.antiaim_jitter_amount), 0.f, 180.f, "%.0f°");
						edited::Checkbox("Spin", "Continuously rotate the yaw", &C_GET(bool, Vars.antiaim_spin));
						if (C_GET(bool, Vars.antiaim_spin))
							edited::SliderFloat("Spin speed", "Degrees per second", &C_GET(float, Vars.antiaim_spin_speed), 1.f, 720.f, "%.0f°/s");

						ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Movement profiles");
						const char* aaProfiles[5]{ "Standing", "Moving", "Airborne", "Crouching", "Slow walking" };
						edited::Combo("Edit profile", "The matching movement state is selected automatically", &C_GET(int, Vars.antiaim_profile_selection), aaProfiles, IM_ARRAYSIZE(aaProfiles), 5);
						const int aaProfile = std::clamp(C_GET(int, Vars.antiaim_profile_selection), 0, 4);
						edited::Checkbox("Enable profile", "Apply this state's additional yaw and jitter", &C_GET_ARRAY(bool, 5, Vars.antiaim_profile_enable, aaProfile));
						if (C_GET_ARRAY(bool, 5, Vars.antiaim_profile_enable, aaProfile))
						{
							edited::SliderFloat("Profile yaw", "State-specific yaw offset", &C_GET_ARRAY(float, 5, Vars.antiaim_profile_yaw, aaProfile), -180.f, 180.f, "%.0f°");
							edited::SliderFloat("Profile jitter", "State-specific jitter amount", &C_GET_ARRAY(float, 5, Vars.antiaim_profile_jitter, aaProfile), 0.f, 180.f, "%.0f°");
						}
						edited::Keybind("Manual back", "Hold to force backward yaw", &C_GET(int, Vars.antiaim_manual_back_key));
						edited::Keybind("Manual forward", "Hold to preserve forward yaw", &C_GET(int, Vars.antiaim_manual_forward_key));
						edited::Checkbox("Disable while using", "Suspend anti-aim while the Use key is held", &C_GET(bool, Vars.antiaim_disable_use));
					}
				edited::EndChild();
			}
			else if (active_tab == 3) {
				edited::BeginChild("##RemovalsContainer", ImVec2(c::background::size.x - 200, c::background::size.y), 0);
				{
					ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Removals");
					edited::Checkbox("Remove Smoke", "Uses the safe particle-render path; other particle effects may also be hidden", &C_GET(bool, Vars.bRemoveSmoke));
					edited::Checkbox("Reduce Flash", "Limits the local flash overlay without touching grenade entities", &C_GET(bool, Vars.bRemoveFlash));
					if (C_GET(bool, Vars.bRemoveFlash))
						edited::SliderFloat("Flash opacity", "Maximum flash overlay alpha", &C_GET(float, Vars.flFlashOpacity), 0.f, 255.f, "%.0f / 255");
					edited::Checkbox("Remove Scope Overlay", "Disables the sniper zoom stencil overlay", &C_GET(bool, Vars.bRemoveScopeOverlay));
					edited::Checkbox("Remove Aim Punch", "Removes camera-only recoil punch; weapon recoil is unchanged", &C_GET(bool, Vars.bRemoveAimPunch));
					edited::Checkbox("Remove Camera Blur", "Disables depth-of-field blur controls", &C_GET(bool, Vars.bRemoveMotionBlur));
				}
				edited::EndChild();
			}
			else if (active_tab == 4) {
				edited::BeginChild("##Container0", ImVec2((c::background::size.x - 200) / 2, c::background::size.y), 0);
				{

					ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Players");

					edited::Checkbox(CS_XOR("Enable"), CS_XOR(""), &C_GET(bool, Vars.bVisualOverlay));
					edited::Checkbox(CS_XOR("Bounding box"), CS_XOR("Shows player bounding box"), &C_GET(FrameOverlayVar_t, Vars.overlayBox).bEnable);
					if (C_GET(FrameOverlayVar_t, Vars.overlayBox).bEnable)
					{
						const char* boxStyles[2]{ CS_XOR("Full"), CS_XOR("Corner") };
						edited::Combo(CS_XOR("Box style"), CS_XOR("Full rectangle or corner segments"),
							&C_GET(int, Vars.esp_box_style), boxStyles, IM_ARRAYSIZE(boxStyles), 2);
						edited::SliderFloat(CS_XOR("Box thickness"), CS_XOR("Visible line thickness"),
							&C_GET(FrameOverlayVar_t, Vars.overlayBox).flThickness, 1.f, 5.f, "%.1f px");
						edited::Color(CS_XOR("##boxprimary"), CS_XOR("Box color"),
							&C_GET(FrameOverlayVar_t, Vars.overlayBox).colPrimary,
							ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
					}
					edited::Checkbox(CS_XOR("Filled box"), CS_XOR("Draws a translucent background inside the player box"), &C_GET(bool, Vars.esp_box_fill));
					if (C_GET(bool, Vars.esp_box_fill))
					{
						edited::Checkbox(CS_XOR("Fill gradient"), CS_XOR("Blends independently configured top and bottom colors"), &C_GET(bool, Vars.esp_box_fill_gradient));
						if (C_GET(bool, Vars.esp_box_fill_gradient))
						{
							edited::Color(CS_XOR("##boxfilltop"), CS_XOR("Filled box top color"), &C_GET(ColorPickerVar_t, Vars.esp_box_fill_top_color).colValue,
								ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
							edited::Color(CS_XOR("##boxfillbottom"), CS_XOR("Filled box bottom color"), &C_GET(ColorPickerVar_t, Vars.esp_box_fill_bottom_color).colValue,
								ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
						}
						else
						edited::Color(CS_XOR("##boxfill"), CS_XOR("Filled box color"), &C_GET(ColorPickerVar_t, Vars.esp_box_fill_color).colValue,
							ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
					}
					edited::Checkbox(CS_XOR("Name"), CS_XOR("Shows player name"), &C_GET(TextOverlayVar_t, Vars.overlayName).bEnable);
					edited::Checkbox(CS_XOR("Health bar"), CS_XOR("Shows player health"), &C_GET(BarOverlayVar_t, Vars.overlayHealthBar).bEnable);
					if (C_GET(BarOverlayVar_t, Vars.overlayHealthBar).bEnable)
					{
						edited::Checkbox(CS_XOR("Health number"), CS_XOR("Shows the exact remaining health"), &C_GET(BarOverlayVar_t, Vars.overlayHealthBar).bShowValue);
						edited::Checkbox(CS_XOR("Health gradient"), CS_XOR("Green at high health, red at low health"), &C_GET(BarOverlayVar_t, Vars.overlayHealthBar).bUseFactorColor);
					}
					edited::Checkbox(CS_XOR("Armor bar"), CS_XOR("Shows current armor below the player"), &C_GET(bool, Vars.esp_armor_bar));
					edited::Checkbox(CS_XOR("Ammo bar"), CS_XOR("Shows player weapon ammo"), &C_GET(BarOverlayVar_t, Vars.AmmoBar).bEnable);
					edited::Checkbox(CS_XOR("Weapon"), CS_XOR("Shows the player's equipped weapon"), &C_GET(TextOverlayVar_t, Vars.Weaponesp).bEnable);
					if (C_GET(TextOverlayVar_t, Vars.Weaponesp).bEnable)
						edited::Checkbox(CS_XOR("Weapon icon"), CS_XOR("Use the equipped gun icon instead of text"), &C_GET(TextOverlayVar_t, Vars.Weaponesp).bIcon);
					edited::Checkbox(CS_XOR("Skeleton"), CS_XOR("Shows player bones as skeleton"), &C_GET(bool, Vars.bSkeleton));

					if (C_GET(bool, Vars.bSkeleton))
					{
						edited::SliderFloat(CS_XOR("Skeleton thickness"), CS_XOR("Thickness of live bone segments"), &C_GET(float, Vars.esp_skeleton_thickness), 1.f, 5.f, "%.1f px");
						edited::Color(CS_XOR("##skeletoncolor"), CS_XOR("Change menu accent color"), &C_GET(ColorPickerVar_t, Vars.colSkeleton).colValue, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
					}
					edited::Checkbox(CS_XOR("Distance"), CS_XOR("Shows distance in metres"), &C_GET(bool, Vars.esp_distance));
					edited::Checkbox(CS_XOR("Head circle"), CS_XOR("Marks the live head bone"), &C_GET(bool, Vars.esp_head_circle));
					edited::Checkbox(CS_XOR("View direction"), CS_XOR("Shows where each player is looking"), &C_GET(bool, Vars.esp_view_direction));
					edited::Checkbox(CS_XOR("Snaplines"), CS_XOR("Connects enemies to the bottom of the screen"), &C_GET(bool, Vars.esp_snaplines));
					edited::Checkbox(CS_XOR("Offscreen arrows"), CS_XOR("Points toward enemies outside the screen"), &C_GET(bool, Vars.esp_offscreen_arrows));

					edited::MultiCombo(CS_XOR("Flags"), &C_GET(unsigned int, Vars.pEspFlags), arrEspFlags, CS_ARRAYSIZE(arrEspFlags));
				
					ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Chams");

					edited::Checkbox(CS_XOR("Player chams"), CS_XOR("Overrides enemy model materials; this is not glow"), &C_GET(bool, Vars.bVisualChams));
					if (C_GET(bool, Vars.bVisualChams))
						edited::Color(CS_XOR("##chamscolor"), CS_XOR("Change chams color"), &C_GET(ColorPickerVar_t, Vars.colVisualChams).colValue, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
					edited::Checkbox(CS_XOR("Through walls"), CS_XOR("Draws a second material pass with depth disabled"), &C_GET(bool, Vars.bVisualChamsIgnoreZ));
					if (C_GET(bool, Vars.bVisualChamsIgnoreZ))
						edited::Color(CS_XOR("##chamscolorxqz"), CS_XOR("Change xqz chams color"), &C_GET(ColorPickerVar_t, Vars.colVisualChamsIgnoreZ).colValue, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
					edited::Checkbox(CS_XOR("Arms chams"), CS_XOR("Overrides first-person arm materials"), &C_GET(bool, Vars.chams_arms));
					if (C_GET(bool, Vars.chams_arms))
						edited::Color(CS_XOR("##armschamscolor"), CS_XOR("Independent arms color"), &C_GET(ColorPickerVar_t, Vars.chams_arms_color).colValue,
							ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
					edited::Checkbox(CS_XOR("Sleeve chams"), CS_XOR("Overrides first-person sleeve materials"), &C_GET(bool, Vars.chams_sleeves));
					if (C_GET(bool, Vars.chams_sleeves))
						edited::Color(CS_XOR("##sleevechamscolor"), CS_XOR("Independent sleeve color"), &C_GET(ColorPickerVar_t, Vars.chams_sleeves_color).colValue,
							ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
					edited::Checkbox(CS_XOR("Held weapon chams"), CS_XOR("Overrides the local first-person weapon"), &C_GET(bool, Vars.chams_held_weapon));
					edited::Checkbox(CS_XOR("Knife chams"), CS_XOR("Overrides knives independently from held-weapon chams"), &C_GET(bool, Vars.chams_knife));
					if (C_GET(bool, Vars.chams_knife))
						edited::Color(CS_XOR("##knifechamscolor"), CS_XOR("Independent knife color"), &C_GET(ColorPickerVar_t, Vars.chams_knife_color).colValue,
							ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
					edited::Checkbox(CS_XOR("Grenade chams"), CS_XOR("Overrides locally owned grenade models"), &C_GET(bool, Vars.chams_grenades));
					edited::Checkbox(CS_XOR("Bomb chams"), CS_XOR("Overrides carried, dropped, and planted C4 even after the planter dies"), &C_GET(bool, Vars.chams_bomb));

				#ifdef _WIN32
					const char* chams[3]{ CS_XOR("Flat"), CS_XOR("Default"),CS_XOR("Illumin") };
					edited::Combo(CS_XOR("Models"), CS_XOR(""), &C_GET(int, Vars.nVisualChamMaterial), chams, IM_ARRAYSIZE(chams), 3);
				#else
						const char* chams[5]{ CS_XOR("Flat"), CS_XOR("Metallic"), CS_XOR("Glow"), CS_XOR("Glass"), CS_XOR("Wireframe") };
						edited::Combo(CS_XOR("Material"), CS_XOR("Select a solid, lit, emissive, translucent, or mesh-line material"),
							&C_GET(int, Vars.nVisualChamMaterial), chams, IM_ARRAYSIZE(chams), 5);
						if (C_GET(int, Vars.nVisualChamMaterial) == 2)
							edited::SliderFloat(CS_XOR("Glow intensity"), CS_XOR("0 is dark/subtle; 100 is the conservative maximum"),
								&C_GET(float, Vars.chams_glow_intensity), 0.f, 100.f, "%.0f%%");
					#endif

						ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "World, grenades and bomb");
						edited::Checkbox("World modulation", "Tint map and prop materials in the native scene draw", &C_GET(bool, Vars.world_color_enable));
						if (C_GET(bool, Vars.world_color_enable))
							edited::Color("##worldcolor", "World tint", &C_GET(ColorPickerVar_t, Vars.world_color).colValue, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
						edited::Checkbox("Sky modulation", "Tint sky materials independently", &C_GET(bool, Vars.sky_color_enable));
						if (C_GET(bool, Vars.sky_color_enable))
							edited::Color("##skycolor", "Sky tint", &C_GET(ColorPickerVar_t, Vars.sky_color).colValue, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
						edited::Checkbox("Dropped weapon ESP", "Label unowned weapons in the world", &C_GET(bool, Vars.dropped_weapon_esp));
						edited::Checkbox("Grenade trajectory", "Predict the held grenade path", &C_GET(bool, Vars.grenade_trajectory));
						edited::Checkbox("Trajectory bounces", "Mark each predicted bounce", &C_GET(bool, Vars.grenade_bounce_markers));
						edited::Checkbox("Trajectory landing", "Mark the predicted final position", &C_GET(bool, Vars.grenade_landing_marker));
						edited::Checkbox("Smoke timer", "Show remaining smoke duration", &C_GET(bool, Vars.smoke_duration_timer));
						edited::Checkbox("Molotov timer", "Show remaining fire duration", &C_GET(bool, Vars.molotov_expiration_timer));
						edited::Checkbox("Bomb timer", "Show planted C4 time and defuse state", &C_GET(bool, Vars.planted_bomb_timer));
						edited::Color("##worldespcolor", "World ESP and trajectory color", &C_GET(ColorPickerVar_t, Vars.world_esp_color).colValue, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);

					}
				edited::EndChild();

				ImGui::SameLine(0, 0);

				edited::BeginChild("##Container1", ImVec2((c::background::size.x - 200 * dpi) / 2, c::background::size.y), 0);
				{
					ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Preview");

					/* render model preview*/
					ImGui::SetCursorPos({ 65, 75 });
					#ifdef _WIN32
					ImGui::Image((void*)I::Maintexture, ImVec2(278, 380));
					#else
					if (g_PreviewTexture != nullptr)
						ImGui::Image((ImTextureID)g_PreviewTexture, ImVec2(278, 380));
					else
						ImGui::Dummy(ImVec2(278, 380));
					#endif

					using namespace F::VISUALS::OVERLAY;

					ImGuiStyle& style = ImGui::GetStyle();
					// @note: call this function inside rendermainwindow, else expect a crash...
					const ImVec2 vecOverlayPadding = ImVec2(65 * dpi, 58 * dpi);  // Adjusted the Y position

					const ImVec2 vecWindowPos = ImGui::GetWindowPos();
					const ImVec2 vecWindowSize = ImGui::GetWindowSize();

					ImDrawList* pDrawList = ImGui::GetWindowDrawList();
					Context_t context;

					ImVec4 vecBox = {
						vecWindowPos.x + vecOverlayPadding.x,
						vecWindowPos.y + vecOverlayPadding.y,
						vecWindowPos.x + vecWindowSize.x - vecOverlayPadding.x,
						vecWindowPos.y + vecWindowSize.y - vecOverlayPadding.y - 10.f
					};

					if (const auto& boxOverlayConfig = C_GET(FrameOverlayVar_t, Vars.overlayBox); boxOverlayConfig.bEnable)
					{
						const bool bHovered = context.AddBoxComponent(pDrawList, vecBox, 1, boxOverlayConfig.flThickness, boxOverlayConfig.flRounding, boxOverlayConfig.colPrimary, boxOverlayConfig.colOutline);

						if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
							ImGui::OpenPopup(CS_XOR("context##box.component"));

						if (ImGui::BeginPopup(CS_XOR("context##box.component"), ImGuiWindowFlags_NoResize))
						{
							ImVec2 size = ImVec2(135, 275);
							ImGui::SetWindowSize(size); 

							edited::Color(CS_XOR("Primary##box.component"), "" , & C_GET(FrameOverlayVar_t, Vars.overlayBox).colPrimary, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar);
							edited::Color(CS_XOR("Outline##box.component"), "", & C_GET(FrameOverlayVar_t, Vars.overlayBox).colOutline, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar);
							ImGui::SliderFloat(CS_XOR("Thickness##box.component"), &C_GET(FrameOverlayVar_t, Vars.overlayBox).flThickness, 1.f, 5.f, CS_XOR("%.1f"), ImGuiSliderFlags_AlwaysClamp);
							ImGui::SliderFloat(CS_XOR("Rounding##box.component"), &C_GET(FrameOverlayVar_t, Vars.overlayBox).flRounding, 1.f, 5.f, CS_XOR("%.1f"), ImGuiSliderFlags_AlwaysClamp);
							ImGui::EndPopup();
						}
					}

					//name
					if (const auto& nameOverlayConfig = C_GET(TextOverlayVar_t, Vars.overlayName); nameOverlayConfig.bEnable)
						context.AddComponent(new CTextComponent(true, false, SIDE_TOP, DIR_TOP, FONT::pVisual, CS_XOR("Name"), Vars.overlayName));

					// health
					if (const auto& healthOverlayConfig = C_GET(BarOverlayVar_t, Vars.overlayHealthBar); healthOverlayConfig.bEnable)
					{
						const float flFactor = M_SIN(ImGui::GetTime() * 5.f) * 0.55f + 0.45f;
						context.AddComponent(new CBarComponent(true, SIDE_LEFT, vecBox, 100.f, flFactor, Vars.overlayHealthBar));
					}

					// weapon
					if (const auto& weaponOverlayConfig = C_GET(TextOverlayVar_t, Vars.Weaponesp); weaponOverlayConfig.bEnable)
					{
						if (weaponOverlayConfig.bIcon && FONT::pEspIcons != nullptr)
							context.AddComponent(new CTextComponent(true, true, SIDE_BOTTOM, DIR_BOTTOM, FONT::pEspIcons, CS_XOR("W"), Vars.Weaponesp));
						else
							context.AddComponent(new CTextComponent(true, true, SIDE_BOTTOM, DIR_BOTTOM, FONT::pVisual, CS_XOR("Weapon"), Vars.Weaponesp));
					}

					// armour
					if (const auto& armorOverlayConfig = C_GET(BarOverlayVar_t, Vars.AmmoBar); armorOverlayConfig.bEnable)
					{
						const float flArmorFactor = M_SIN(ImGui::GetTime() * 5.f) * 0.55f + 0.45f;
						context.AddComponent(new CBarComponent(true, SIDE_BOTTOM, vecBox, 32.f, flArmorFactor, Vars.AmmoBar));
					}

					// flags 
					{
						if (C_GET(unsigned int, Vars.pEspFlags) & FLAGS_ARMOR) {

							if (const auto& hkcfg = C_GET(TextOverlayVar_t, Vars.HKFlag); hkcfg.bEnable)
								context.AddComponent(new CTextComponent(true, false, SIDE_RIGHT, DIR_RIGHT, FONT::pEspWepName, CS_XOR("HK"), Vars.HKFlag));
						}

						if (C_GET(unsigned int, Vars.pEspFlags) & FLAGS_DEFUSER) {

							if (const auto& kitcfg = C_GET(TextOverlayVar_t, Vars.KitFlag); kitcfg.bEnable)
								context.AddComponent(new CTextComponent(true, false, SIDE_RIGHT, DIR_BOTTOM, FONT::pEspWepName, CS_XOR("KIT"), Vars.KitFlag));
						}
					}
					// only render context preview if overlay is enabled
					context.Render(pDrawList, vecBox);

				}
				edited::EndChild();
			}
			else if (active_tab == 6) {
			edited::BeginChild("##Container0", ImVec2((c::background::size.x - 200) / 2, c::background::size.y), 0);
			{

				ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Players");

				edited::Checkbox(CS_XOR("Anti untrusted"), CS_XOR(""), &C_GET(bool, Vars.bAntiUntrusted));
				edited::Checkbox(CS_XOR("Thirdperson"), CS_XOR("Puts you in thirdperson"), &C_GET(bool, Vars.bThirdperson));
				if (C_GET(bool, Vars.bThirdperson))
				{
					edited::Keybind("Thirdperson key", "Press once to toggle the camera", &C_GET(int, Vars.thirdperson_ui_key));
					edited::SliderFloat(CS_XOR("Thirdperson distance"), CS_XOR("Thirdperson cam distance"), &C_GET(float, Vars.flThirdperson), 50.f, 300.f, "%.0f");
					edited::Checkbox(CS_XOR("Thirdperson collision"), CS_XOR("Prevents the camera passing through walls"), &C_GET(bool, Vars.thirdperson_collision));
				}

				edited::Checkbox(CS_XOR("FOV Changer"), CS_XOR("Makes your FOV bigger"), &C_GET(bool, Vars.bFOV));
				if (C_GET(bool, Vars.bFOV))
				{
					edited::SliderFloat(CS_XOR("FOV Amount"), CS_XOR("How much you change your FOV"), &C_GET(float, Vars.fFOVAmount), 30.f, 150.f);
				}

				edited::Checkbox(CS_XOR("View FOV Changer"), CS_XOR("Makes Arms Far"), &C_GET(bool, Vars.bSetViewModelFOV));
				if (C_GET(bool, Vars.bSetViewModelFOV))
				{
					edited::SliderFloat(CS_XOR("View FOV Amount"), CS_XOR("Amount"), &C_GET(float, Vars.flSetViewModelFOV), 40.f, 150.f);
				}

				edited::Checkbox(CS_XOR("Bunny hop"), CS_XOR("Releases jump in air and reapplies it on landing while Space is held"), &C_GET(bool, Vars.bAutoBHop));
				if (C_GET(bool, Vars.bAutoBHop))
					edited::SliderInt(CS_XOR("Bunny hop chance"), CS_XOR("Chance to jump on each landing"), &C_GET(int, Vars.nAutoBHopChance), 1, 100, "%d%%");
				edited::Checkbox(CS_XOR("Auto strafer"), CS_XOR("Applies alternating air-strafe input through CreateMove"), &C_GET(bool, Vars.bAutostrafe));
				if (C_GET(bool, Vars.bAutostrafe))
				{
					edited::SliderFloat(CS_XOR("Strafe strength"), CS_XOR("Side-movement strength"), &C_GET(float, Vars.autostrafe_smooth), 1.f, 100.f, "%.0f%%");
					edited::MultiCombo(CS_XOR("Strafe modes"), &C_GET(unsigned int, Vars.bAutostrafeMode), arrMovementStrafer, CS_ARRAYSIZE(arrMovementStrafer));
				}
					#ifdef _WIN32
					edited::Checkbox(CS_XOR("Edge bug"), CS_XOR("Edge bug"), &C_GET(bool, Vars.edge_bug));
					#endif
					edited::Color(CS_XOR("##menuaccent"), CS_XOR("Change menu accent color"), &color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
					#ifdef __linux__
					ImGui::Separator();
					ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Native runtime health");
					ImGui::Text("CreateMove: %s | calls: %llu | angle writes: %llu",
						IsNativeInputHookInstalled() ? "ready" : "waiting",
						GetNativeCreateMoveCalls(), GetNativeAimAngleApplications());
					ImGui::Text("Trace/collision: %s", TRACE::NativeReady() ? "self-test passed" : "fail-closed / waiting");
					ImGui::Text("Swapchain: %ux%u | rebuilds: %llu | failures: %llu | retirements: %llu | present faults: %llu",
						GetVulkanRuntimeWidth(), GetVulkanRuntimeHeight(),
						GetVulkanSwapchainRebuilds(), GetVulkanSwapchainRebuildFailures(),
						GetVulkanRendererRetirements(), GetVulkanPresentFaults());
					ImGui::Text("Chams: %s | enemy meshes: %llu | knife meshes: %llu | bomb meshes: %llu | bomb target: %s",
						NativeChams::IsInstalled() ? "ready" : "waiting",
						NativeChams::GetEnemyMeshMatches(), NativeChams::GetKnifeMeshMatches(),
						NativeChams::GetBombMeshMatches(), NativeChams::HasBombTarget() ? "tracked" : "none");
					ImGui::TextWrapped("Runtime-only rows in SELECTED_FEATURES.md still require an actual match test before they are marked verified.");
					#endif

			}
			edited::EndChild();
			ImGui::SameLine(0, 0);
			ImGui::SetCursorPos(ImVec2(527, 60));
			edited::BeginChild("##Container1", ImVec2((c::background::size.x - 200 * dpi) / 2, c::background::size.y), 0);
			{
				ImGui::Columns(2, CS_XOR("#CONFIG"), false);
				{
					ImGui::PushItemWidth(-1);

					// Repair stale selection after refresh/removal and select the
					// default (or first available) configuration.
					if (nSelectedConfig >= C::vecFileNames.size())
					{
						nSelectedConfig = ~0ULL;
						for (std::size_t i = 0U; i < C::vecFileNames.size(); i++)
						{
							if (CRT::StringCompare(C::vecFileNames[i], CS_XOR(CS_CONFIGURATION_DEFAULT_FILE_NAME CS_CONFIGURATION_FILE_EXTENSION)) == 0)
							{
								nSelectedConfig = i;
								break;
							}
						}
						if (nSelectedConfig == ~0ULL && !C::vecFileNames.empty())
							nSelectedConfig = 0U;
					}

					if (ImGui::BeginListBox(CS_XOR("##config.list"), C::vecFileNames.size(), 5))
					{
						for (std::size_t i = 0U; i < C::vecFileNames.size(); i++)
						{
							// Convert wide string to narrow string
							const std::wstring wideName = C::vecFileNames[i];
							constexpr std::size_t bufferSize = 512;
							char narrowName[bufferSize];
							const std::size_t converted = std::wcstombs(narrowName, wideName.c_str(), bufferSize - 1U);
							if (converted == static_cast<std::size_t>(-1))
								CRT::StringCopy(narrowName, "invalid filename");
							else
								narrowName[converted] = '\0';

							if (ImGui::Selectable(narrowName, (nSelectedConfig == i)))
								nSelectedConfig = i;
						}

						ImGui::EndListBox();
					}

					ImGui::PopItemWidth();
				}
				ImGui::NextColumn();
				{
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 3.f * MENU::flDpiScale));
					ImGui::PushItemWidth(-1);
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.10f, 0.95f));
					const bool createOnEnter = ImGui::InputTextWithHint(CS_XOR("##config.file"), "config name...", szConfigFile, sizeof(szConfigFile), ImGuiInputTextFlags_EnterReturnsTrue);
					ImGui::PopStyleColor(2);
					if (ImGui::Button(CS_XOR("create"), ImVec2(-1, 15 * MENU::flDpiScale)) || createOnEnter)
					{
						// check if the filename isn't empty
						if (const std::size_t nConfigFileLength = CRT::StringLengthN(szConfigFile, sizeof(szConfigFile)); nConfigFileLength > 0U)
						{
							wchar_t wszConfigFile[65] = {};
							const std::size_t wideLength = std::min(nConfigFileLength, CS_ARRAYSIZE(wszConfigFile) - 1U);
							for (std::size_t index = 0; index < wideLength; ++index)
								wszConfigFile[index] = static_cast<unsigned char>(szConfigFile[index]);

							if (C::CreateFile(wszConfigFile))
							{
								std::wstring createdName(wszConfigFile);
								if (createdName.size() < 4U || createdName.substr(createdName.size() - 4U) != CS_CONFIGURATION_FILE_EXTENSION)
									createdName += CS_CONFIGURATION_FILE_EXTENSION;
								nSelectedConfig = ~0ULL;
								for (std::size_t i = 0U; i < C::vecFileNames.size(); ++i)
									if (createdName == C::vecFileNames[i])
									{
										nSelectedConfig = i;
										break;
									}
								NOTIFY::Push({ N_TYPE_SUCCESS, CS_XOR("config created") });
							}
							else
								NOTIFY::Push({ N_TYPE_ERROR, CS_XOR("invalid, duplicate, or unwritable config name") });

							// clear string
							CRT::MemorySet(szConfigFile, 0U, sizeof(szConfigFile));
						}
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip(CS_XOR("type a name, then click create or press enter"));

					const bool hasSelectedConfig = nSelectedConfig < C::vecFileNames.size();
					ImGui::BeginDisabled(!hasSelectedConfig);
					if (ImGui::Button(CS_XOR("rename"), ImVec2(-1, 15 * MENU::flDpiScale)))
					{
						if (CRT::StringLengthN(szConfigFile, sizeof(szConfigFile)) > 0U)
						{
							wchar_t newName[65] = {};
							const std::size_t nameLength = std::min(
								CRT::StringLengthN(szConfigFile, sizeof(szConfigFile)),
								CS_ARRAYSIZE(newName) - 1U);
							for (std::size_t index = 0; index < nameLength; ++index)
								newName[index] = static_cast<unsigned char>(szConfigFile[index]);
							if (C::RenameFile(nSelectedConfig, newName))
							{
								NOTIFY::Push({ N_TYPE_SUCCESS, CS_XOR("config renamed") });
								CRT::MemorySet(szConfigFile, 0U, sizeof(szConfigFile));
							}
							else
								NOTIFY::Push({ N_TYPE_ERROR, CS_XOR("config rename failed") });
						}
					}
					if (ImGui::Button(CS_XOR("save"), ImVec2(-1, 15 * MENU::flDpiScale)))
					{
						if (C::SaveFile(nSelectedConfig))
							NOTIFY::Push({ N_TYPE_SUCCESS, CS_XOR("config saved") });
						else
							NOTIFY::Push({ N_TYPE_ERROR, CS_XOR("config save failed") });
					}
					if (ImGui::Button(CS_XOR("load"), ImVec2(-1, 15 * MENU::flDpiScale)))
					{
						if (C::LoadFile(nSelectedConfig))
							NOTIFY::Push({ N_TYPE_SUCCESS, CS_XOR("config loaded") });
						else
							NOTIFY::Push({ N_TYPE_ERROR, CS_XOR("config is invalid or outdated") });
					}
					if (ImGui::Button(CS_XOR("remove"), ImVec2(-1, 15 * MENU::flDpiScale)))
					{
						ImGui::OpenPopup(CS_XOR("confirmation##config.remove"));
					}
					ImGui::EndDisabled();
					if (ImGui::Button(CS_XOR("refresh"), ImVec2(-1, 15 * MENU::flDpiScale)))
					{
						C::Refresh();
						nSelectedConfig = ~0ULL;
						NOTIFY::Push({ N_TYPE_INFO, CS_XOR("configs refreshed") });
					}
					ImGui::PopItemWidth();
					ImGui::PopStyleVar();
				}
				ImGui::Columns(1);

				if (ImGui::BeginPopupModal(CS_XOR("confirmation##config.remove"), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
				{
					if (nSelectedConfig >= C::vecFileNames.size())
					{
						ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
					}
					else
					{
						CRT::String_t<MAX_PATH> szCurrentConfig(C::vecFileNames[nSelectedConfig]);

						ImGui::Text(CS_XOR("are you sure you want to remove \"%s\" configuration?"), szCurrentConfig.Data());
						ImGui::Spacing();

						if (ImGui::Button(CS_XOR("no"), ImVec2(ImGui::GetContentRegionAvail().x / 2.f, 0)))
						{
							ImGui::CloseCurrentPopup();
							NOTIFY::Push({ N_TYPE_ERROR, CS_XOR("canceled") });
						}
						ImGui::SameLine();

						if (ImGui::Button(CS_XOR("yes"), ImVec2(ImGui::GetContentRegionAvail().x, 0)))
						{
							if (C::RemoveFile(nSelectedConfig))
							{
								nSelectedConfig = ~0ULL;
								NOTIFY::Push({ N_TYPE_WARNING, CS_XOR("config removed") });
							}
							else
								NOTIFY::Push({ N_TYPE_ERROR, CS_XOR("config remove failed") });

							ImGui::CloseCurrentPopup();
						}

						ImGui::EndPopup();
					}
				}
			}
			edited::EndChild();
			}
			else if (active_tab == 5) {
				#ifdef __linux__
					edited::BeginChild("##NativeSkinChanger", ImVec2(600, c::background::size.y), 0);
					{
						ImGui::TextColored(ImColor(ImGui::GetColorU32(c::elements::text)), "Native skin changer");
						ImGui::TextWrapped("Each weapon keeps its own saved fallback-skin profile. Inventory injection remains disabled until normal weapon overrides are stable.");
						edited::Checkbox("Enable native skin changer", "Applies only profiles enabled for their matching weapon definition", &C_GET(bool, Vars.skin_ui_enable));
						ImGui::TextDisabled("Status: %s", LinuxNativeEsp::GetSkinRuntimeStatus());

						const std::uint16_t definition = LinuxNativeEsp::GetActiveSkinWeaponDefinition();
						if (definition == 0 || definition >= LinuxNativeEsp::SkinWeaponDefinitionCount)
						{
							ImGui::TextDisabled("Hold a weapon in a live match to edit its profile.");
						}
						else
						{
							ImGui::Separator();
							ImGui::Text("Active: %s (definition %u)", LinuxNativeEsp::GetActiveSkinWeaponName(), definition);
							bool& enabled = C_GET_ARRAY(bool, 1024, Vars.skin_weapon_enable, definition);
							edited::Checkbox("Override this weapon", "The profile follows this weapon definition across switches and respawns", &enabled);
							if (enabled)
							{
								ImGui::SetNextItemWidth(240.f);
								ImGui::InputInt("Paint kit ID", &C_GET_ARRAY(int, 1024, Vars.skin_weapon_paint_kit, definition));
								ImGui::SetNextItemWidth(240.f);
								ImGui::SliderInt("Pattern seed", &C_GET_ARRAY(int, 1024, Vars.skin_weapon_seed, definition), 0, 1000);
								ImGui::SetNextItemWidth(240.f);
								ImGui::SliderFloat("Wear", &C_GET_ARRAY(float, 1024, Vars.skin_weapon_wear, definition), 0.000001f, 1.f, "%.6f", ImGuiSliderFlags_Logarithmic);
								ImGui::SetNextItemWidth(240.f);
								ImGui::InputInt("StatTrak (-1 off)", &C_GET_ARRAY(int, 1024, Vars.skin_weapon_stattrak, definition));
								edited::Checkbox("Legacy model mesh", "Enable for finishes that require the older weapon mesh group", &C_GET_ARRAY(bool, 1024, Vars.skin_weapon_legacy_mesh, definition));
								if (C_GET(bool, Vars.skin_ui_enable) &&
									C_GET_ARRAY(int, 1024, Vars.skin_weapon_paint_kit, definition) > 0 &&
									ImGui::Button("Apply / refresh now"))
								{
									LinuxNativeEsp::RequestSkinRefresh();
								}
								ImGui::TextDisabled("Use a paint kit supported by this exact weapon; knives do not change model and gloves are not included yet.");
							}
						}
					}
					edited::EndChild();
				#else
					edited::BeginChild("##Container0", ImVec2((600), c::background::size.y), 0);
				{					
					if (edited::Button(CS_XOR("Full update"), ImVec2(120, 50), 0)) {
						Vars.full_update = true;
						
					}
						
					if (pItemSchema != nullptr && vecDumpedItems.empty() && edited::Button(CS_XOR("Dump items"),  ImVec2(120, 50), 0)) {


						const CUtlMap<int, CEconItemDefinition*>& vecItems =
							pItemSchema->GetSortedItemDefinitionMap();
						CUtlMap<int, CPaintKit*>& vecPaintKits =
							pItemSchema->GetPaintKits();
						const CUtlMap<uint64_t, AlternateIconData_t>& vecAlternateIcons =
							pItemSchema->GetAlternateIconsMap();

						for (const auto& it : vecItems) {
							CEconItemDefinition* pItem = it.m_value;
							if (!pItem) continue;

							const bool isWeapon = pItem->IsWeapon();
							const std::string gloveType = CS_XOR("#Type_Hands");
							const std::string knifeType = CS_XOR("#CSGO_Type_Knife");
					
							bool isGlove = (pItem->m_pszItemBaseName == gloveType);
							bool isKnife = (pItem->m_pszItemBaseName == knifeType);

							const char* itemBaseName = pItem->GetSimpleWeaponName();


							if (!itemBaseName || itemBaseName[0] == '\0') continue;

							const uint16_t defIdx = pItem->m_nDefIndex;

							DumpedItem_t dumpedItem;
							dumpedItem.m_name = I::Localize->FindSafe(itemBaseName);
							dumpedItem.m_image = pItem->m_pKVItem;
							dumpedItem.m_defIdx = defIdx;
							dumpedItem.m_rarity = pItem->m_nItemRarity;
							if (isKnife | isGlove) {
								dumpedItem.m_unusualItem = true;
							}


							// Load the image and set the texture ID.
							if (dumpedItem.m_image) {
								dumpedItem.m_textureID = CreateTextureFromMemory(dumpedItem.m_image, 120, 280);
							}

							// We filter skins by guns.
							for (const auto& it : vecPaintKits) {
								CPaintKit* pPaintKit = it.m_value;
								if (!pPaintKit || pPaintKit->PaintKitId() == 0 || pPaintKit->PaintKitId() == 9001)
									continue;

								const uint64_t skinKey =
									Helper_GetAlternateIconKeyForWeaponPaintWearItem(
										defIdx, pPaintKit->PaintKitId(), 0);
								if (vecAlternateIcons.FindByKey(skinKey)) {
									DumpedSkin_t dumpedSkin;
									dumpedSkin.m_name = I::Localize->FindSafe(
										pPaintKit->PaintKitDescriptionTag());
									dumpedSkin.m_ID = pPaintKit->PaintKitId();
									dumpedSkin.m_rarity = pPaintKit->PaintKitRarity();
									dumpedItem.m_dumpedSkins.emplace_back(dumpedSkin);
								}
							}

							// Sort skins by rarity.
							if (!dumpedItem.m_dumpedSkins.empty() && isWeapon) {
								std::sort(dumpedItem.m_dumpedSkins.begin(),
									dumpedItem.m_dumpedSkins.end(),
									[](const DumpedSkin_t& a, const DumpedSkin_t& b) {
										return a.m_rarity > b.m_rarity;
									});
							}

							vecDumpedItems.emplace_back(dumpedItem);
						}
					}
					static char IconFilterText[128] = "";


					if (!vecDumpedItems.empty()) {
						if (edited::Button("Add all items", ImVec2(120, 50), 0)) {
							for (const auto& item : vecDumpedItems) {
								for (const auto& skin : item.m_dumpedSkins) {
									CEconItem* pItem = CEconItem::CreateInstance();
									L_PRINT(LOG_INFO) << "item addr:" << L::AddFlags(LOG_MODE_INT_FORMAT_HEX | LOG_MODE_INT_SHOWBASE) << reinterpret_cast<uintptr_t>(pItem);
									if (pItem) {
										CCSPlayerInventory* pInventory =
											CCSPlayerInventory::GetInstance();
										auto highestIDs = pInventory->GetHighestIDs();
										L_PRINT(LOG_INFO) << "uid:" << pItem->m_ulID << " id:" << pItem->m_unAccountID << "idx:" << pItem->m_unDefIndex;
										pItem->m_ulID = highestIDs.first + 1;
										pItem->m_unInventory = highestIDs.second + 1;
										pItem->m_unAccountID =
											uint32_t(pInventory->GetOwner().m_id);
										pItem->m_unDefIndex = item.m_defIdx;
										if (item.m_unusualItem) pItem->m_nQuality = IQ_UNUSUAL;
										pItem->m_nRarity =
											std::clamp(item.m_rarity + skin.m_rarity - 1, 0,
												(skin.m_rarity == 7) ? 7 : 6);

										pItem->SetPaintKit((float)skin.m_ID);
										pItem->SetPaintSeed(1.f);
										if (pInventory->AddEconItem(pItem))
											skin_changer::AddEconItemToList(pItem);
									}
								}
							}
						}

					}

					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Will cause lag on weaker computers.");
					if (!vecDumpedItems.empty()) {

						static ImGuiTextFilter itemFilter;
						itemFilter.Draw("Type here to filter Items...", windowWidth);
					

						// ...

						// Modify the loop for items to check against the item filter.
						if (ImGui::BeginListBox("##items", { windowWidth, 110.f })) {
							for (auto& item : vecDumpedItems) {
								if (!itemFilter.PassFilter(item.m_name.c_str()))
									continue;

								ImGui::PushID(&item);
								if (ImGui::Selectable(item.m_name.c_str(), pSelectedItem == &item)) {
									if (pSelectedItem == &item)
										pSelectedItem = nullptr;
									else
										pSelectedItem = &item;
								}
								ImGui::PopID();
							}
							ImGui::EndListBox();
						}
						static char skinFilterText[128] = "";

						if (pSelectedItem) {
							if (!pSelectedItem->m_dumpedSkins.empty()) {


								static ImGuiTextFilter skinFilter;
								skinFilter.Draw("Type here to filter Skins...", windowWidth);

								if (ImGui::BeginListBox("##skins", { windowWidth, 110.f })) {
									for (auto& skin : pSelectedItem->m_dumpedSkins) {
										if (!skinFilter.PassFilter(skin.m_name.c_str()))
											continue;

										ImGui::PushID(&skin);
										if (ImGui::Selectable(
											skin.m_name.c_str(),
											pSelectedItem->pSelectedSkin == &skin)) {
											if (pSelectedItem->pSelectedSkin == &skin)
												pSelectedItem->pSelectedSkin = nullptr;
											else
												pSelectedItem->pSelectedSkin = &skin;
										}
										ImGui::PopID();
									}
									ImGui::EndListBox();
								}
							}

							char buttonLabel[128];
							snprintf(buttonLabel, 128, "Add every %s skin",
								pSelectedItem->m_name.c_str());

							if (edited::Button(buttonLabel, ImVec2(120, 55), 0)) {
								for (const auto& skin : pSelectedItem->m_dumpedSkins) {
									CEconItem* pItem = CEconItem::CreateInstance();
									if (pItem) {
										CCSPlayerInventory* pInventory =
											CCSPlayerInventory::GetInstance();

										auto highestIDs = pInventory->GetHighestIDs();

										pItem->m_ulID = highestIDs.first + 1;
										pItem->m_unInventory = highestIDs.second + 1;
										pItem->m_unAccountID =
											uint32_t(pInventory->GetOwner().m_id);
										pItem->m_unDefIndex = pSelectedItem->m_defIdx;
										if (pSelectedItem->m_unusualItem)
											pItem->m_nQuality = IQ_UNUSUAL;
										pItem->m_nRarity = std::clamp(
											pSelectedItem->m_rarity + skin.m_rarity - 1, 0,
											(skin.m_rarity == 7) ? 7 : 6);

										pItem->SetPaintKit((float)skin.m_ID);
										pItem->SetPaintSeed(1.f);
										if (pInventory->AddEconItem(pItem))
											skin_changer::AddEconItemToList(pItem);
									}
								}
							}
							ImGui::SameLine();
							if (pSelectedItem->pSelectedSkin) {
								static float kitWear = 0.f;
								static int kitSeed = 1;
								static int gunKills = -1;
								static char gunName[32];

								bool vanillaSkin = pSelectedItem->pSelectedSkin->m_ID == 0;
								snprintf(
									buttonLabel, 128, "Add %s%s%s",
									pSelectedItem->m_name.c_str(), vanillaSkin ? "" : " | ",
									vanillaSkin ? ""
									: pSelectedItem->pSelectedSkin->m_name.c_str());

								if (edited::Button(buttonLabel, ImVec2(120, 55), 0)) {
									CEconItem* pItem = CEconItem::CreateInstance();
									if (pItem) {
										CCSPlayerInventory* pInventory =
											CCSPlayerInventory::GetInstance();

										auto highestIDs = pInventory->GetHighestIDs();
										L_PRINT(LOG_INFO) << "item addr:" << L::AddFlags(LOG_MODE_INT_FORMAT_HEX | LOG_MODE_INT_SHOWBASE) << reinterpret_cast<uintptr_t>(pItem);
										L_PRINT(LOG_INFO) << "uid:" << pItem->m_ulID << " id:" << pItem->m_unAccountID << "idx:" << pItem->m_unDefIndex;

										pItem->m_ulID = highestIDs.first + 1;
										pItem->m_unInventory = highestIDs.second + 1;
										pItem->m_unAccountID =
											uint32_t(pInventory->GetOwner().m_id);
										pItem->m_unDefIndex = pSelectedItem->m_defIdx;

										if (pSelectedItem->m_unusualItem)
											pItem->m_nQuality = IQ_UNUSUAL;

										// I don't know nor do care why the rarity is calculated
										// like this. [Formula]
										pItem->m_nRarity = std::clamp(
											pSelectedItem->m_rarity +
											pSelectedItem->pSelectedSkin->m_rarity - 1,
											0,
											(pSelectedItem->pSelectedSkin->m_rarity == 7) ? 7
											: 6);

										pItem->SetPaintKit(
											(float)pSelectedItem->pSelectedSkin->m_ID);
										pItem->SetPaintSeed((float)kitSeed);
										pItem->SetPaintWear(kitWear);

										if (gunKills >= 0) {
											pItem->SetStatTrak(gunKills);
											pItem->SetStatTrakType(0);

											// Applied automatically on knives.
											if (pItem->m_nQuality != IQ_UNUSUAL)
												pItem->m_nQuality = IQ_STRANGE;
										}

										if (pInventory->AddEconItem(pItem))
											skin_changer::AddEconItemToList(pItem);

										kitWear = 0.f;
										kitSeed = 1;
										gunKills = -1;
										memset(gunName, '\0', IM_ARRAYSIZE(gunName));
									}
								}

								ImGui::Dummy({ 0, 8 });
								ImGui::SeparatorText("Extra settings");

								ImGui::TextUnformatted("Wear Rating");
								ImGui::SetNextItemWidth(windowWidth);
								ImGui::SliderFloat("##slider1", &kitWear, 0.f, 1.f, "%.9f",
									ImGuiSliderFlags_Logarithmic);

								ImGui::TextUnformatted("Pattern Template");
								ImGui::SetNextItemWidth(windowWidth);
								ImGui::SliderInt("##slider2", &kitSeed, 1, 1000);

								ImGui::TextUnformatted("StatTrak Count");
								ImGui::SetNextItemWidth(windowWidth);
								ImGui::SliderInt("##slider3", &gunKills, -1, INT_MAX / 2,
									gunKills == -1 ? "Not StatTrak" : "%d",
									ImGuiSliderFlags_Logarithmic);

								ImGui::TextUnformatted("Custom Name");
								ImGui::SetNextItemWidth(windowWidth);
								ImGui::InputTextWithHint("##input1", "Default", gunName,
									IM_ARRAYSIZE(gunName));
							}
						}
					}
				}
					edited::EndChild();
				#endif

			}
		}
		

		ImGui::PopStyleVar();
	}
	ImGui::End();

	ImGui::PopStyleVar();

}


void MENU::RenderWatermark()
{
	if (!C_GET(bool, Vars.bWatermark) || !bMainWindowOpened)
		return;

	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.f, 0.f, 0.f, 0.03f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.03f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 0.03f));
	ImGui::PushFont(FONT::pExtra);
	ImGui::BeginMainMenuBar();
	{
		ImGui::Dummy(ImVec2(1, 1));

#ifdef _DEBUG
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), CS_XOR("debug"));
#endif
		if (CRT::StringString(GetCommandLineW(), CS_XOR(L"-insecure")) != nullptr)
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), CS_XOR("insecure"));

		if (
#ifdef _WIN32
			I::Engine->IsInGame()
#else
			false
#endif
		)
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), CS_XOR("in-game"));

		static ImVec2 vecNameSize = ImGui::CalcTextSize(CS_XOR("cs2project | " __DATE__ " " __TIME__));
		ImGui::SameLine(ImGui::GetContentRegionMax().x - vecNameSize.x - style.FramePadding.x);
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), CS_XOR("cs2project | " __DATE__ " " __TIME__));
	}
	ImGui::EndMainMenuBar();
	ImGui::PopFont();
	ImGui::PopStyleColor(3);
}

void MENU::UpdateStyle(ImGuiStyle* pStyle)
{
	ImGuiStyle& style = pStyle != nullptr ? *pStyle : ImGui::GetStyle();

	style.Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
	style.Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
	style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.14f, 0.87f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.26f, 0.26f, 0.40f);
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.26f, 0.67f);
	style.Colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.29f, 0.48f, 1.00f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	style.Colors[ImGuiCol_ScrollbarBg] =  c::tab::tab_active;
	style.Colors[ImGuiCol_ScrollbarGrab] = c::tab::tab_active;
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = c::accent;
	style.Colors[ImGuiCol_ScrollbarGrabActive] = c::accent;
	style.Colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_SliderGrab] = ImVec4(c::accent.x, c::accent.y, c::accent.z, 0.70f);
	style.Colors[ImGuiCol_SliderGrabActive] = c::accent;
	style.Colors[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_Separator] = style.Colors[ImGuiCol_Border];
	style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
	style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 0.00f);
	style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0);
	style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0);
	style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0);
	style.Colors[ImGuiCol_Tab] = ImLerp(style.Colors[ImGuiCol_Header], style.Colors[ImGuiCol_TitleBgActive], 0.80f);
	style.Colors[ImGuiCol_TabHovered] = style.Colors[ImGuiCol_HeaderHovered];
	style.Colors[ImGuiCol_TabActive] = ImLerp(style.Colors[ImGuiCol_HeaderActive], style.Colors[ImGuiCol_TitleBgActive], 0.60f);
	style.Colors[ImGuiCol_TabUnfocused] = ImLerp(style.Colors[ImGuiCol_Tab], style.Colors[ImGuiCol_TitleBg], 0.80f);
	style.Colors[ImGuiCol_TabUnfocusedActive] = ImLerp(style.Colors[ImGuiCol_TabActive], style.Colors[ImGuiCol_TitleBg], 0.40f);
	style.Colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);


	C_GET(ColorPickerVar_t, Vars.colPrimtv0).UpdateRainbow(); // (text)
	C_GET(ColorPickerVar_t, Vars.colPrimtv1).UpdateRainbow(); // (background)
	C_GET(ColorPickerVar_t, Vars.colPrimtv2).UpdateRainbow(); // (disabled)
	C_GET(ColorPickerVar_t, Vars.colPrimtv3).UpdateRainbow(); // (control bg)
	C_GET(ColorPickerVar_t, Vars.colPrimtv4).UpdateRainbow(); // (border)

	C_GET(ColorPickerVar_t, Vars.colAccent0).UpdateRainbow(); // (main)
	C_GET(ColorPickerVar_t, Vars.colAccent1).UpdateRainbow(); // (dark)
	C_GET(ColorPickerVar_t, Vars.colAccent2).UpdateRainbow(); // (darker)

	// update animation speed
	style.AnimationSpeed = C_GET(float, Vars.flAnimationSpeed) / 10.f;
}
static void RenderInventoryWindow() {
	static constexpr float windowWidth = 540.f;

	struct DumpedSkin_t {
		std::string m_name = "";
		int m_ID = 0;
		int m_rarity = 0;
	};
	struct DumpedItem_t {
		std::string m_name = "";
		uint16_t m_defIdx = 0;
		int m_rarity = 0;
		bool m_unusualItem = false;
		std::vector<DumpedSkin_t> m_dumpedSkins{};
		DumpedSkin_t* pSelectedSkin = nullptr;
	};
	static std::vector<DumpedItem_t> vecDumpedItems;
	static DumpedItem_t* pSelectedItem = nullptr;

	CEconItemSchema* pItemSchema = nullptr;
#ifdef _WIN32
	pItemSchema = I::Client->GetEconItemSystem()->GetEconItemSchema();
#endif


	if (ImGui::Begin("cs2sdk item dumper", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (pItemSchema != nullptr && vecDumpedItems.empty() &&
			ImGui::Button("Dump items", { windowWidth, 0 })) {
		

			const CUtlMap<int, CEconItemDefinition*>& vecItems =
				pItemSchema->GetSortedItemDefinitionMap();
			const CUtlMap<int, CPaintKit*>& vecPaintKits =
				pItemSchema->GetPaintKits();
			const CUtlMap<uint64_t, AlternateIconData_t>& vecAlternateIcons =
				pItemSchema->GetAlternateIconsMap();

			for (const auto& it : vecItems) {
				CEconItemDefinition* pItem = it.m_value;
				if (!pItem) continue;

				const bool isWeapon = pItem->IsWeapon();
				
				const bool isKnife = pItem->m_pszItemTypeName != nullptr &&
					std::strcmp(pItem->m_pszItemTypeName, "#CSGO_Type_Knife") == 0;
			//	auto isGloves = pItem->IsGlove(true, pItem->m_pszItemTypeName);
				 
				if (!isWeapon && !isKnife) continue;

				// Some items don't have names.
				const char* itemBaseName = pItem->m_pszItemBaseName;
				if (!itemBaseName || itemBaseName[0] == '\0') continue;

				const uint16_t defIdx = pItem->m_nDefIndex;

				DumpedItem_t dumpedItem;
				dumpedItem.m_name = I::Localize->FindSafe(itemBaseName);
				dumpedItem.m_defIdx = defIdx;
				dumpedItem.m_rarity = pItem->m_nItemRarity;
				if (isKnife) {
					dumpedItem.m_unusualItem = true;
				}

			

				// We filter skins by guns.
				for (const auto& it : vecPaintKits) {
					CPaintKit* pPaintKit = it.m_value;
					if (!pPaintKit || pPaintKit->PaintKitId() == 0 || pPaintKit->PaintKitId() == 9001)
						continue;

					const uint64_t skinKey =
						Helper_GetAlternateIconKeyForWeaponPaintWearItem(
							defIdx, pPaintKit->PaintKitId(), 0);
					if (vecAlternateIcons.FindByKey(skinKey)) {
						DumpedSkin_t dumpedSkin;
						dumpedSkin.m_name = I::Localize->FindSafe(
							pPaintKit->PaintKitDescriptionTag());
						dumpedSkin.m_ID = pPaintKit->PaintKitId();
						dumpedSkin.m_rarity = pPaintKit->PaintKitRarity();
						dumpedItem.m_dumpedSkins.emplace_back(dumpedSkin);
					}
				}

				// Sort skins by rarity.
				if (!dumpedItem.m_dumpedSkins.empty() && isWeapon) {
					std::sort(dumpedItem.m_dumpedSkins.begin(),
						dumpedItem.m_dumpedSkins.end(),
						[](const DumpedSkin_t& a, const DumpedSkin_t& b) {
							return a.m_rarity > b.m_rarity;
						});
				}

				vecDumpedItems.emplace_back(dumpedItem);
			}
		}


		if (!vecDumpedItems.empty()) {
			if (ImGui::Button("Add all items", { windowWidth, 0.f })) {
				for (const auto& item : vecDumpedItems) {
					for (const auto& skin : item.m_dumpedSkins) {
						CEconItem* pItem = CEconItem::CreateInstance();
						L_PRINT(LOG_INFO) << "item addr:" << L::AddFlags(LOG_MODE_INT_FORMAT_HEX | LOG_MODE_INT_SHOWBASE) << reinterpret_cast<uintptr_t>(pItem);
						if (pItem) {
							CCSPlayerInventory* pInventory =
								CCSPlayerInventory::GetInstance();

							auto highestIDs = pInventory->GetHighestIDs();
							L_PRINT(LOG_INFO) << "uid:" << pItem->m_ulID << " id:" << pItem->m_unAccountID << "idx:" << pItem->m_unDefIndex;
							pItem->m_ulID = highestIDs.first + 1;
							pItem->m_unInventory = highestIDs.second + 1;
							pItem->m_unAccountID =
								uint32_t(pInventory->GetOwner().m_id);
							pItem->m_unDefIndex = item.m_defIdx;
							if (item.m_unusualItem) pItem->m_nQuality = IQ_UNUSUAL;
							pItem->m_nRarity =
								std::clamp(item.m_rarity + skin.m_rarity - 1, 0,
									(skin.m_rarity == 7) ? 7 : 6);

							pItem->SetPaintKit((float)skin.m_ID);
							pItem->SetPaintSeed(1.f);
							if (pInventory->AddEconItem(pItem))
								skin_changer::AddEconItemToList(pItem);
						}
					}
				}
			}

		}

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Will cause lag on weaker computers.");

		static ImGuiTextFilter itemFilter;
		itemFilter.Draw("##filter", windowWidth);

		if (ImGui::BeginListBox("##items", { windowWidth, 140.f })) {
			for (auto& item : vecDumpedItems) {
				if (!itemFilter.PassFilter(item.m_name.c_str())) continue;

				ImGui::PushID(&item);
				if (ImGui::Selectable(item.m_name.c_str(),
					pSelectedItem == &item)) {
					if (pSelectedItem == &item)
						pSelectedItem = nullptr;
					else
						pSelectedItem = &item;
				}
				ImGui::PopID();
			}
			ImGui::EndListBox();
		}

		if (pSelectedItem) {
			if (!pSelectedItem->m_dumpedSkins.empty()) {
				static ImGuiTextFilter skinFilter;
				skinFilter.Draw("##filter2", windowWidth);

				if (ImGui::BeginListBox("##skins", { windowWidth, 140.f })) {
					for (auto& skin : pSelectedItem->m_dumpedSkins) {
						if (!skinFilter.PassFilter(skin.m_name.c_str()))
							continue;

						ImGui::PushID(&skin);
						if (ImGui::Selectable(
							skin.m_name.c_str(),
							pSelectedItem->pSelectedSkin == &skin)) {
							if (pSelectedItem->pSelectedSkin == &skin)
								pSelectedItem->pSelectedSkin = nullptr;
							else
								pSelectedItem->pSelectedSkin = &skin;
						}
						ImGui::PopID();
					}
					ImGui::EndListBox();
				}
			}

			char buttonLabel[128];
			snprintf(buttonLabel, 128, "Add every %s skin",
				pSelectedItem->m_name.c_str());

			if (ImGui::Button(buttonLabel, { windowWidth, 0.f })) {
				for (const auto& skin : pSelectedItem->m_dumpedSkins) {
					CEconItem* pItem = CEconItem::CreateInstance();
					if (pItem) {
						CCSPlayerInventory* pInventory =
							CCSPlayerInventory::GetInstance();

						auto highestIDs = pInventory->GetHighestIDs();

						pItem->m_ulID = highestIDs.first + 1;
						pItem->m_unInventory = highestIDs.second + 1;
						pItem->m_unAccountID =
							uint32_t(pInventory->GetOwner().m_id);
						pItem->m_unDefIndex = pSelectedItem->m_defIdx;
						if (pSelectedItem->m_unusualItem)
							pItem->m_nQuality = IQ_UNUSUAL;
						pItem->m_nRarity = std::clamp(
							pSelectedItem->m_rarity + skin.m_rarity - 1, 0,
							(skin.m_rarity == 7) ? 7 : 6);

						pItem->SetPaintKit((float)skin.m_ID);
						pItem->SetPaintSeed(1.f);
						if (pInventory->AddEconItem(pItem))
							skin_changer::AddEconItemToList(pItem);
					}
				}
			}

			if (pSelectedItem->pSelectedSkin) {
				static float kitWear = 0.f;
				static int kitSeed = 1;
				static int gunKills = -1;
				static char gunName[32];

				bool vanillaSkin = pSelectedItem->pSelectedSkin->m_ID == 0;
				snprintf(
					buttonLabel, 128, "Add %s%s%s",
					pSelectedItem->m_name.c_str(), vanillaSkin ? "" : " | ",
					vanillaSkin ? ""
					: pSelectedItem->pSelectedSkin->m_name.c_str());

				if (ImGui::Button(buttonLabel, { windowWidth, 0.f })) {
					CEconItem* pItem = CEconItem::CreateInstance();
					if (pItem) {
						CCSPlayerInventory* pInventory =
							CCSPlayerInventory::GetInstance();

						auto highestIDs = pInventory->GetHighestIDs();
						L_PRINT(LOG_INFO) << "item addr:" << L::AddFlags(LOG_MODE_INT_FORMAT_HEX | LOG_MODE_INT_SHOWBASE) << reinterpret_cast<uintptr_t>(pItem);
						L_PRINT(LOG_INFO) << "uid:" << pItem->m_ulID << " id:" << pItem->m_unAccountID << "idx:" << pItem->m_unDefIndex;

						pItem->m_ulID = highestIDs.first + 1;
						pItem->m_unInventory = highestIDs.second + 1;
						pItem->m_unAccountID =
							uint32_t(pInventory->GetOwner().m_id);
						pItem->m_unDefIndex = pSelectedItem->m_defIdx;

						if (pSelectedItem->m_unusualItem)
							pItem->m_nQuality = IQ_UNUSUAL;

						// I don't know nor do care why the rarity is calculated
						// like this. [Formula]
						pItem->m_nRarity = std::clamp(
							pSelectedItem->m_rarity +
							pSelectedItem->pSelectedSkin->m_rarity - 1,
							0,
							(pSelectedItem->pSelectedSkin->m_rarity == 7) ? 7
							: 6);

						pItem->SetPaintKit(
							(float)pSelectedItem->pSelectedSkin->m_ID);
						pItem->SetPaintSeed((float)kitSeed);
						pItem->SetPaintWear(kitWear);

						if (gunKills >= 0) {
							pItem->SetStatTrak(gunKills);
							pItem->SetStatTrakType(0);

							// Applied automatically on knives.
							if (pItem->m_nQuality != IQ_UNUSUAL)
								pItem->m_nQuality = IQ_STRANGE;
						}

						if (pInventory->AddEconItem(pItem))
							skin_changer::AddEconItemToList(pItem);

						kitWear = 0.f;
						kitSeed = 1;
						gunKills = -1;
						memset(gunName, '\0', IM_ARRAYSIZE(gunName));
					}
				}

				ImGui::Dummy({ 0, 8 });
				ImGui::SeparatorText("Extra settings");

				ImGui::TextUnformatted("Wear Rating");
				ImGui::SetNextItemWidth(windowWidth);
				ImGui::SliderFloat("##slider1", &kitWear, 0.f, 1.f, "%.9f",
					ImGuiSliderFlags_Logarithmic);

				ImGui::TextUnformatted("Pattern Template");
				ImGui::SetNextItemWidth(windowWidth);
				ImGui::SliderInt("##slider2", &kitSeed, 1, 1000);

				ImGui::TextUnformatted("StatTrak Count");
				ImGui::SetNextItemWidth(windowWidth);
				ImGui::SliderInt("##slider3", &gunKills, -1, INT_MAX / 2,
					gunKills == -1 ? "Not StatTrak" : "%d",
					ImGuiSliderFlags_Logarithmic);

				ImGui::TextUnformatted("Custom Name");
				ImGui::SetNextItemWidth(windowWidth);
				ImGui::InputTextWithHint("##input1", "Default", gunName,
					IM_ARRAYSIZE(gunName));
			}
		}
	}

	ImGui::End();
}


#pragma region menu_tabs

void T::Render(const char* szTabBar, const CTab* arrTabs, const unsigned long long nTabsCount, int* nCurrentTab, ImGuiTabBarFlags flags)
{
	if (ImGui::BeginTabBar(szTabBar, flags))
	{
		for (std::size_t i = 0U; i < nTabsCount; i++)
		{
			// add tab
			if (ImGui::BeginTabItem(arrTabs[i].szName))
			{
				// set current tab index
				*nCurrentTab = (int)i;
				ImGui::EndTabItem();
			}
		}

		// render inner tab
		if (arrTabs[*nCurrentTab].pRenderFunction != nullptr)
			arrTabs[*nCurrentTab].pRenderFunction();

		ImGui::EndTabBar();
	}
}


#pragma endregion

#pragma region menu_particle

void MENU::ParticleContext_t::Render(ImDrawList* pDrawList, const ImVec2& vecScreenSize, const float flAlpha)
{
	if (this->vecParticles.empty())
	{
		for (int i = 0; i < 100; i++)
			this->AddParticle(ImGui::GetIO().DisplaySize);
	}

	for (auto& particle : this->vecParticles)
	{
		this->DrawParticle(pDrawList, particle, C_GET(ColorPickerVar_t, Vars.colAccent0).colValue.Set<COLOR_A>(flAlpha * 255));
		this->UpdatePosition(particle, vecScreenSize);
		this->FindConnections(pDrawList, particle, C_GET(ColorPickerVar_t, Vars.colAccent2).colValue.Set<COLOR_A>(flAlpha * 255), 200.f);
	}
}

void MENU::ParticleContext_t::AddParticle(const ImVec2& vecScreenSize)
{
	// exceeded limit
	if (this->vecParticles.size() >= 200UL)
		return;

	// @note: random speed value
	static constexpr float flSpeed = 100.f;
	this->vecParticles.emplace_back(
	ImVec2(MATH::fnRandomFloat(0.f, vecScreenSize.x), MATH::fnRandomFloat(0.f, vecScreenSize.y)),
	ImVec2(MATH::fnRandomFloat(-flSpeed, flSpeed), MATH::fnRandomFloat(-flSpeed, flSpeed)));
}

void MENU::ParticleContext_t::DrawParticle(ImDrawList* pDrawList, ParticleData_t& particle, const Color_t& colPrimary)
{
	D::AddDrawListCircle(pDrawList, particle.vecPosition, 2.f, colPrimary, 12, DRAW_CIRCLE_OUTLINE | DRAW_CIRCLE_FILLED);
}

void MENU::ParticleContext_t::FindConnections(ImDrawList* pDrawList, ParticleData_t& particle, const Color_t& colPrimary, float flMaxDistance)
{
	for (auto& currentParticle : this->vecParticles)
	{
		// skip current particle
		if (&currentParticle == &particle)
			continue;

		/// @note: calculate length distance 2d
		ImVec2 delta = ImVec2(particle.vecPosition.x - currentParticle.vecPosition.x, particle.vecPosition.y - currentParticle.vecPosition.y);
		const float flDistance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
		if (flDistance <= flMaxDistance)
			this->DrawConnection(pDrawList, particle, currentParticle, (flMaxDistance - flDistance) / flMaxDistance, colPrimary);
	}
}

void MENU::ParticleContext_t::DrawConnection(ImDrawList* pDrawList, ParticleData_t& particle, ParticleData_t& otherParticle, float flAlpha, const Color_t& colPrimary) const
{
	D::AddDrawListLine(pDrawList, particle.vecPosition, otherParticle.vecPosition, colPrimary.Set<COLOR_A>(flAlpha * 255), 1.f);
}

void MENU::ParticleContext_t::UpdatePosition(ParticleData_t& particle, const ImVec2& vecScreenSize) const
{
	this->ResolveScreenCollision(particle, vecScreenSize);

	ImGuiStyle& style = ImGui::GetStyle();

	// move particle
	particle.vecPosition.x += (particle.vecVelocity.x * style.AnimationSpeed * 10.f) * ImGui::GetIO().DeltaTime;
	particle.vecPosition.y += (particle.vecVelocity.y * style.AnimationSpeed * 10.f) * ImGui::GetIO().DeltaTime;
}

void MENU::ParticleContext_t::ResolveScreenCollision(ParticleData_t& particle, const ImVec2& vecScreenSize) const
{
	if (particle.vecPosition.x + particle.vecVelocity.x > vecScreenSize.x || particle.vecPosition.x + particle.vecVelocity.x < 0)
		particle.vecVelocity.x = -particle.vecVelocity.x;

	if (particle.vecPosition.y + particle.vecVelocity.y > vecScreenSize.y || particle.vecPosition.y + particle.vecVelocity.y < 0)
		particle.vecVelocity.y = -particle.vecVelocity.y;
}

#pragma endregion
