// TGHMod v2 (草稿，未经实机测试)
// 方案：不换玩家模型、不碰内存地址。
//   T = 灵魂出窍：Niko 原地冻结，镜头升到空中（Q/E 旋转镜头）
//   G = 附身：目标 NPC 倒地抽搐 1.8 秒，然后玩家用 WASD 操控它（Shift 跑，Q/E 转镜头）
//   H = 返回：镜头回到 Niko，NPC 恢复普通 AI
//
// 标注 [待确认] 的 native 名称/参数，请对照你手里 Scripting.h 里的实际声明，
// 编译报错时把报错发给我，我再对着改。

#include <windows.h>
#include <cmath>
#include "ScriptHookManager.h"
#include "Scripting.h"

using namespace Scripting;

enum class State { Idle, Spirit, Convulse, Control };

static State g_state = State::Idle;
static Ped g_niko = 0;
static Ped g_target = 0;
static Cam g_cam = 0;
static bool g_camCreated = false;
static float g_camYaw = 0.0f;       // 镜头朝向（度）
static DWORD g_convulseEnd = 0;
static DWORD g_nextMoveTask = 0;
static bool g_wasMoving = false;

static const DWORD CONVULSE_MS = 1800;
static const float PI_F = 3.14159265f;

// ---------- 输入 ----------
static bool GameFocused() {
    HWND h = GetForegroundWindow();
    if (!h) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

static bool KeyDown(int vk) {
    return GameFocused() && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// 只在按下的那一帧返回 true
static bool Pressed(int vk) {
    static bool prev[256] = {};
    bool now = KeyDown(vk);
    bool r = now && !prev[vk];
    prev[vk] = now;
    return r;
}

// ---------- 镜头 ----------
static void CamStart() {
    if (g_camCreated) return;
    CreateCam(14, &g_cam);                 // [待确认] 14 为脚本镜头类型
    SetCamActive(g_cam, true);
    ActivateScriptedCams(true, true);      // [待确认]
    g_camCreated = true;
}

static void CamStop() {
    if (!g_camCreated) return;
    ActivateScriptedCams(false, false);
    SetCamActive(g_cam, false);
    DestroyCam(g_cam);
    g_camCreated = false;
}

static void CamUpdate(float tx, float ty, float tz, float dist, float height) {
    float h = g_camYaw * PI_F / 180.0f;
    float fx = -sinf(h), fy = cosf(h);
    SetCamPos(g_cam, tx - fx * dist, ty - fy * dist, tz + height);
    PointCamAtCoord(g_cam, tx, ty, tz + 0.8f);
}

static void RotateCamByKeys() {
    if (KeyDown('Q')) g_camYaw += 1.5f;
    if (KeyDown('E')) g_camYaw -= 1.5f;
}

// ---------- 状态切换 ----------
static void ReturnToNiko() {
    if (g_target && DoesCharExist(g_target)) {
        ClearCharTasks(g_target);
        SetBlockingOfNonTemporaryEvents(g_target, false);   // [待确认]
    }
    CamStop();
    if (g_niko && DoesCharExist(g_niko)) {
        FreezeCharPosition(g_niko, false);
    }
    SetPlayerControl(GetPlayerId(), true);
    g_target = 0;
    g_wasMoving = false;
    g_state = State::Idle;
}

static void SpiritOn() {
    if (g_state != State::Idle) return;
    GetPlayerChar(GetPlayerId(), &g_niko);
    if (!g_niko || !DoesCharExist(g_niko)) return;

    float x = 0, y = 0, z = 0;
    GetCharCoordinates(g_niko, &x, &y, &z);
    float heading = 0;
    GetCharHeading(g_niko, &heading);
    g_camYaw = heading;

    SetPlayerControl(GetPlayerId(), false);   // 关掉玩家对 Niko 的控制
    FreezeCharPosition(g_niko, true);         // Niko 的身体留在原地
    CamStart();
    g_state = State::Spirit;
}

static bool FindTarget(Ped* out) {
    float x = 0, y = 0, z = 0;
    GetCharCoordinates(g_niko, &x, &y, &z);

    Ped cand = 0;
    // 找 Niko 附近 15 米内最近的人 [待确认：各 bool 参数的含义，目的是排除 Niko 自己]
    GetClosestChar(x, y, z, 15.0f, true, false, &cand);

    if (!cand || cand == g_niko) return false;
    if (!DoesCharExist(cand) || IsCharDead(cand)) return false;
    *out = cand;
    return true;
}

static void Possess() {
    if (g_state != State::Spirit) return;
    Ped t = 0;
    if (!FindTarget(&t)) return;

    g_target = t;
    ClearCharTasksImmediately(g_target);
    SetBlockingOfNonTemporaryEvents(g_target, true);   // 防止它被吓跑或还手 [待确认]

    // 倒地抽搐：布娃娃 [待确认：参数个数和含义，请对照 Scripting.h]
    SwitchPedToRagdoll(g_target, (int)CONVULSE_MS, (int)CONVULSE_MS + 500, 0, true, true, false);

    g_convulseEnd = GetTickCount() + CONVULSE_MS;
    g_state = State::Convulse;
}

// ---------- 每帧更新 ----------
static void UpdateConvulse() {
    if (!g_target || !DoesCharExist(g_target) || IsCharDead(g_target)) { ReturnToNiko(); return; }

    float x = 0, y = 0, z = 0;
    GetCharCoordinates(g_target, &x, &y, &z);
    CamUpdate(x, y, z, 4.0f, 2.0f);

    if ((int)(g_convulseEnd - GetTickCount()) <= 0) {
        SwitchPedToAnimated(g_target, true);           // [待确认]
        g_state = State::Control;
    }
}

static void UpdateControl() {
    if (!g_target || !DoesCharExist(g_target) || IsCharDead(g_target)) { ReturnToNiko(); return; }

    RotateCamByKeys();

    float x = 0, y = 0, z = 0;
    GetCharCoordinates(g_target, &x, &y, &z);
    CamUpdate(x, y, z, 5.0f, 2.2f);

    float f = (KeyDown('W') ? 1.0f : 0.0f) - (KeyDown('S') ? 1.0f : 0.0f);
    float r = (KeyDown('D') ? 1.0f : 0.0f) - (KeyDown('A') ? 1.0f : 0.0f);

    if (f != 0.0f || r != 0.0f) {
        float h = g_camYaw * PI_F / 180.0f;
        float fx = -sinf(h), fy = cosf(h);   // 镜头前方
        float rx = cosf(h),  ry = sinf(h);   // 镜头右方
        float dx = fx * f + rx * r;
        float dy = fy * f + ry * r;
        float len = sqrtf(dx * dx + dy * dy);
        dx /= len; dy /= len;

        // 每 150ms 才下一次指令，避免每帧重置任务造成卡顿
        DWORD now = GetTickCount();
        if ((int)(now - g_nextMoveTask) >= 0) {
            int moveState = KeyDown(VK_SHIFT) ? 3 : 2;   // [待确认] 2=走 3=跑
            TaskGoStraightToCoord(g_target, x + dx * 4.0f, y + dy * 4.0f, z, moveState, -1);
            g_nextMoveTask = now + 150;
        }
        g_wasMoving = true;
    } else if (g_wasMoving) {
        ClearCharTasks(g_target);
        g_wasMoving = false;
    }
}

static void UpdateSpirit() {
    if (!g_niko || !DoesCharExist(g_niko) || IsCharDead(g_niko)) { ReturnToNiko(); return; }

    RotateCamByKeys();
    float x = 0, y = 0, z = 0;
    GetCharCoordinates(g_niko, &x, &y, &z);
    CamUpdate(x, y, z, 6.0f, 5.0f);   // 俯视 Niko 的身体
}

// ---------- 主循环 ----------
static void ScriptMain() {
    for (;;) {
        if (Pressed('T')) SpiritOn();
        if (Pressed('G')) Possess();
        if (Pressed('H')) { if (g_state != State::Idle) ReturnToNiko(); }

        switch (g_state) {
            case State::Spirit:   UpdateSpirit();   break;
            case State::Convulse: UpdateConvulse(); break;
            case State::Control:  UpdateControl();  break;
            default: break;
        }
        scriptWait(0);
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        scriptRegister(hinstDLL, ScriptMain);
    }
    return TRUE;
}
