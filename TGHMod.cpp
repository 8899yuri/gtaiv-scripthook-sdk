#include "CustomThread.h"

#include "../ScriptHook/Scripting.h"
#include "../ScriptHook/Log.h"

#include <windows.h>
#include <math.h>

using namespace Scripting;

namespace
{
    static const int KEY_SPIRIT  = 'T';
    static const int KEY_POSSESS = 'G';
    static const int KEY_RETURN  = 'H';

    static const int KEY_FORWARD = 'W';
    static const int KEY_BACK    = 'S';
    static const int KEY_LEFT    = 'A';
    static const int KEY_RIGHT   = 'D';
    static const int KEY_RUN     = VK_SHIFT;

    enum TGHMode
    {
        TGH_NORMAL = 0,
        TGH_SPIRIT,
        TGH_POSSESSED
    };

    static TGHMode g_Mode = TGH_NORMAL;

    static Ped g_Niko = {};
    static Ped g_PossessedPed = {};

    static Camera g_SpiritCamera = {};
    static bool g_SpiritCameraCreated = false;

    static f32 g_SpiritX = 0.0f;
    static f32 g_SpiritY = 0.0f;
    static f32 g_SpiritZ = 0.0f;
    static f32 g_SpiritYaw = 0.0f;

    static DWORD g_RagdollEndTime = 0;

    static bool IsKeyJustPressed(int key)
    {
        return (GetAsyncKeyState(key) & 1) != 0;
    }

    static bool IsKeyPressed(int key)
    {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }

    static bool IsPedUsable(Ped ped)
    {
        if (ped.IsNull())
            return false;

        if (!DoesCharExist(ped))
            return false;

        if (IsCharDead(ped))
            return false;

        return true;
    }

    static void GetNiko()
    {
        Player playerIndex =
            ConvertIntToPlayerIndex(GetPlayerId());

        GetPlayerChar(playerIndex, &g_Niko);
    }

    static void DestroySpiritCamera()
    {
        if (!g_SpiritCameraCreated)
            return;

        if (DoesCamExist(g_SpiritCamera))
        {
            SetCamActive(g_SpiritCamera, false);
            DestroyCam(g_SpiritCamera);
        }

        g_SpiritCamera = {};
        g_SpiritCameraCreated = false;
    }

    static bool CreateSpiritCamera()
    {
        DestroySpiritCamera();

        g_SpiritCamera = {};

        CreateCam(14, &g_SpiritCamera);

        if (!DoesCamExist(g_SpiritCamera))
        {
            LogInfo("TGHMod: CreateCam failed");
            g_SpiritCamera = {};
            return false;
        }

        g_SpiritCameraCreated = true;

        SetCamPos(
            g_SpiritCamera,
            g_SpiritX,
            g_SpiritY,
            g_SpiritZ
        );

        SetCamRot(
            g_SpiritCamera,
            0.0f,
            0.0f,
            g_SpiritYaw
        );

        SetCamFov(g_SpiritCamera, 70.0f);
        SetCamPropagate(g_SpiritCamera, true);
        SetCamActive(g_SpiritCamera, true);

        return true;
    }

    static void StartSpiritMode()
    {
        if (g_Mode != TGH_NORMAL)
            return;

        GetNiko();

        if (!IsPedUsable(g_Niko))
        {
            LogInfo("TGHMod: Niko is not available");
            return;
        }

        f32 x;
        f32 y;
        f32 z;
        f32 heading;

        GetCharCoordinates(g_Niko, &x, &y, &z);
        GetCharHeading(g_Niko, &heading);

        g_SpiritX = x;
        g_SpiritY = y;
        g_SpiritZ = z + 1.5f;
        g_SpiritYaw = heading;

        FreezeCharPosition(g_Niko, true);

        if (!CreateSpiritCamera())
        {
            FreezeCharPosition(g_Niko, false);
            return;
        }

        g_Mode = TGH_SPIRIT;

        LogInfo("TGHMod: Spirit mode started");
    }

    static void UpdateSpiritMode()
    {
        if (!IsPedUsable(g_Niko))
            return;

        if (!g_SpiritCameraCreated)
            return;

        if (IsKeyPressed('Q'))
            g_SpiritYaw -= 2.0f;

        if (IsKeyPressed('E'))
            g_SpiritYaw += 2.0f;

        if (g_SpiritYaw >= 360.0f)
            g_SpiritYaw -= 360.0f;

        if (g_SpiritYaw < 0.0f)
            g_SpiritYaw += 360.0f;

        SetCamPos(
            g_SpiritCamera,
            g_SpiritX,
            g_SpiritY,
            g_SpiritZ
        );

        SetCamRot(
            g_SpiritCamera,
            0.0f,
            0.0f,
            g_SpiritYaw
        );
    }

    static bool FindNearbyPed(Ped *result)
    {
        if (result == 0)
            return false;

        *result = {};

        GetNiko();

        if (!IsPedUsable(g_Niko))
            return false;

        f32 x;
        f32 y;
        f32 z;

        GetCharCoordinates(g_Niko, &x, &y, &z);

        Ped target = {};

        bool found = GetClosestChar(
            x,
            y,
            z,
            8.0f,
            false,
            false,
            &target
        );

        if (!found)
            return false;

        if (!IsPedUsable(target))
            return false;

        if (target == g_Niko)
            return false;

        *result = target;
        return true;
    }

    static void StartPossession()
    {
        if (g_Mode != TGH_SPIRIT)
            return;

        Ped target = {};

        if (!FindNearbyPed(&target))
        {
            LogInfo("TGHMod: no nearby NPC found");
            return;
        }

        g_PossessedPed = target;

        SetCharAsMissionChar(g_PossessedPed);

        SetBlockingOfNonTemporaryEvents(
            g_PossessedPed,
            true
        );

        SwitchPedToRagdoll(
            g_PossessedPed,
            1800,
            1800,
            0,
            true,
            true,
            false
        );

        g_RagdollEndTime = GetTickCount() + 1800;

        g_Mode = TGH_POSSESSED;

        DestroySpiritCamera();

        LogInfo("TGHMod: possession started");
    }

    static void UpdatePossessedMovement()
    {
        if (!IsPedUsable(g_PossessedPed))
        {
            g_PossessedPed = {};
            g_Mode = TGH_NORMAL;
            return;
        }

        if (GetTickCount() < g_RagdollEndTime)
            return;

        if (IsPedRagdoll(g_PossessedPed))
        {
            SwitchPedToAnimated(
                g_PossessedPed,
                true
            );
        }

        f32 forward = 0.0f;
        f32 right = 0.0f;

        if (IsKeyPressed(KEY_FORWARD))
            forward += 1.0f;

        if (IsKeyPressed(KEY_BACK))
            forward -= 1.0f;

        if (IsKeyPressed(KEY_RIGHT))
            right += 1.0f;

        if (IsKeyPressed(KEY_LEFT))
            right -= 1.0f;

        if (forward == 0.0f && right == 0.0f)
        {
            SetCharVelocity(
                g_PossessedPed,
                0.0f,
                0.0f,
                0.0f
            );
            return;
        }

        Camera gameCamera = {};

        GetGameCam(&gameCamera);

        f32 angleX;
        f32 angleY;
        f32 angleZ;

        GetCamRot(
            gameCamera,
            &angleX,
            &angleY,
            &angleZ
        );

        const f32 PI = 3.14159265358979323846f;
        const f32 radians = angleZ * PI / 180.0f;

        f32 dirX = cosf(radians);
        f32 dirY = sinf(radians);

        f32 sideX = -sinf(radians);
        f32 sideY = cosf(radians);

        f32 moveX =
            dirX * forward +
            sideX * right;

        f32 moveY =
            dirY * forward +
            sideY * right;

        f32 length =
            sqrtf(
                moveX * moveX +
                moveY * moveY
            );

        if (length > 0.001f)
        {
            moveX /= length;
            moveY /= length;
        }

        f32 speed = 1.5f;

        if (IsKeyPressed(KEY_RUN))
            speed = 3.0f;

        SetCharVelocity(
            g_PossessedPed,
            moveX * speed,
            moveY * speed,
            0.0f
        );

        f32 heading;

        GetHeadingFromVector2D(
            moveX,
            moveY,
            &heading
        );

        SetCharHeading(
            g_PossessedPed,
            heading
        );
    }

    static void ReturnToNiko()
    {
        if (g_Mode == TGH_NORMAL)
            return;

        DestroySpiritCamera();

        if (IsPedUsable(g_PossessedPed))
        {
            SetCharVelocity(
                g_PossessedPed,
                0.0f,
                0.0f,
                0.0f
            );

            ClearCharTasks(g_PossessedPed);

            SetBlockingOfNonTemporaryEvents(
                g_PossessedPed,
                false
            );

            MarkCharAsNoLongerNeeded(
                &g_PossessedPed
            );
        }

        g_PossessedPed = {};

        GetNiko();

        if (IsPedUsable(g_Niko))
        {
            FreezeCharPosition(
                g_Niko,
                false
            );

            SetCamBehindPed(g_Niko);
        }

        g_Mode = TGH_NORMAL;

        LogInfo("TGHMod: returned to Niko");
    }
}

void CustomThread::RunTick()
{
    GetNiko();

    if (IsKeyJustPressed(KEY_RETURN))
    {
        ReturnToNiko();
        return;
    }

    if (g_Mode == TGH_NORMAL)
    {
        if (IsKeyJustPressed(KEY_SPIRIT))
            StartSpiritMode();

        return;
    }

    if (g_Mode == TGH_SPIRIT)
    {
        UpdateSpiritMode();

        if (IsKeyJustPressed(KEY_POSSESS))
            StartPossession();

        return;
    }

    if (g_Mode == TGH_POSSESSED)
    {
        UpdatePossessedMovement();
        return;
    }
}
