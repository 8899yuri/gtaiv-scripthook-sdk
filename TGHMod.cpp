#include <windows.h>
#include <cmath>

#include "ScriptThread.h"
#include "Scripting.h"
#include "ScriptHookManager.h"

using namespace Scripting;

class TGHThread : public ScriptThread
{
private:
    enum Mode
    {
        MODE_NIKO = 0,
        MODE_SPIRIT,
        MODE_POSSESSED
    };

    Mode m_Mode;

    Player m_Player;
    Ped m_Niko;
    Ped m_Target;
    Camera m_Camera;

    f32 m_SpiritYaw;
    f32 m_SpiritPitch;

    u32 m_RagdollEndTime;

    bool m_CameraCreated;

public:
    TGHThread()
    {
        char name[] = "TGHThread";
        SetName(name);

        m_Mode = MODE_NIKO;

        m_Player = 0;
        m_Niko = {};
        m_Target = {};
        m_Camera = {};

        m_SpiritYaw = 0.0f;
        m_SpiritPitch = -25.0f;

        m_RagdollEndTime = 0;
        m_CameraCreated = false;
    }

    virtual ~TGHThread()
    {
    }

protected:

    virtual void RunTick()
    {
        if (m_Player == 0)
        {
            m_Player = ConvertIntToPlayerIndex(GetPlayerId());
        }

        GetPlayerChar(m_Player, &m_Niko);

        if (!DoesCharExist(m_Niko))
            return;

        /*
         * T:
         * Niko -> Spirit
         * Possessed -> Spirit
         */
        if ((GetAsyncKeyState('T') & 1) != 0)
        {
            if (m_Mode == MODE_NIKO)
            {
                EnterSpirit();
            }
            else if (m_Mode == MODE_POSSESSED)
            {
                LeavePossessedToSpirit();
            }
        }

        /*
         * G:
         * Spirit -> nearby NPC
         */
        if ((GetAsyncKeyState('G') & 1) != 0)
        {
            if (m_Mode == MODE_SPIRIT)
            {
                PossessNearestPed();
            }
        }

        /*
         * H:
         * Spirit / Possessed -> Niko
         */
        if ((GetAsyncKeyState('H') & 1) != 0)
        {
            if (m_Mode != MODE_NIKO)
            {
                ReturnToNiko();
            }
        }

        if (m_Mode == MODE_SPIRIT)
        {
            UpdateSpirit();
        }
        else if (m_Mode == MODE_POSSESSED)
        {
            UpdatePossessed();
        }
    }

    virtual void OnKill()
    {
        RestoreEverything();
    }

private:

    void CreateCamera()
    {
        if (m_CameraCreated)
            return;

        m_Camera = {};

        CreateCam(14, &m_Camera);

        if (DoesCamExist(m_Camera))
        {
            SetCamFov(m_Camera, 65.0f);
            SetCamPropagate(m_Camera, true);
            SetCamActive(m_Camera, true);

            m_CameraCreated = true;
        }
    }

    void DestroyCamera()
    {
        if (!m_CameraCreated)
            return;

        if (DoesCamExist(m_Camera))
        {
            SetCamActive(m_Camera, false);
            SetCamPropagate(m_Camera, false);
            DestroyCam(m_Camera);
        }

        m_Camera = {};
        m_CameraCreated = false;
    }

    void EnterSpirit()
    {
        m_Mode = MODE_SPIRIT;

        /*
         * Completely stop Niko.
         */
        FreezeCharPosition(m_Niko, true);
        ClearCharTasksImmediately(m_Niko);

        /*
         * Remove player control.
         */
        SetPlayerControl(m_Player, false);
        SetCameraControlsDisabledWithPlayerControls(false);

        /*
         * Start camera above Niko.
         */
        f32 x;
        f32 y;
        f32 z;
        f32 heading;

        GetCharCoordinates(m_Niko, &x, &y, &z);
        GetCharHeading(m_Niko, &heading);

        m_SpiritYaw = heading;
        m_SpiritPitch = -25.0f;

        CreateCamera();

        if (!m_CameraCreated)
            return;

        UpdateSpiritCamera(x, y, z);
    }

    void UpdateSpirit()
    {
        /*
         * Q / E rotate spirit camera.
         */
        if (GetAsyncKeyState('Q') & 0x8000)
        {
            m_SpiritYaw -= 2.5f;
        }

        if (GetAsyncKeyState('E') & 0x8000)
        {
            m_SpiritYaw += 2.5f;
        }

        if (m_SpiritYaw < 0.0f)
            m_SpiritYaw += 360.0f;

        if (m_SpiritYaw >= 360.0f)
            m_SpiritYaw -= 360.0f;

        f32 x;
        f32 y;
        f32 z;

        GetCharCoordinates(m_Niko, &x, &y, &z);

        UpdateSpiritCamera(x, y, z);
    }

    void UpdateSpiritCamera(f32 x, f32 y, f32 z)
    {
        if (!m_CameraCreated)
            return;

        const f32 DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

        f32 yaw = m_SpiritYaw * DEG_TO_RAD;

        /*
         * Camera is behind and above Niko.
         */
        const f32 distance = 5.0f;
        const f32 height = 4.0f;

        f32 camX = x - std::sin(yaw) * distance;
        f32 camY = y - std::cos(yaw) * distance;
        f32 camZ = z + height;

        SetCamPos(
            m_Camera,
            camX,
            camY,
            camZ
        );

        SetCamRot(
            m_Camera,
            m_SpiritPitch,
            0.0f,
            m_SpiritYaw
        );

        SetCamActive(m_Camera, true);
        SetCamPropagate(m_Camera, true);
    }

    void PossessNearestPed()
    {
        f32 x;
        f32 y;
        f32 z;

        GetCharCoordinates(m_Niko, &x, &y, &z);

        Ped nearest = {};

        /*
         * Search for a nearby pedestrian.
         */
        if (!GetClosestChar(
            x,
            y,
            z,
            4.0f,
            false,
            false,
            &nearest))
        {
            return;
        }

        if (!DoesCharExist(nearest))
            return;

        /*
         * Do not possess Niko himself.
         */
        if (nearest == m_Niko)
            return;

        /*
         * Do not possess dead peds.
         */
        if (IsCharDead(nearest))
            return;

        /*
         * Save target.
         */
        m_Target = nearest;

        /*
         * Give this ped to the script.
         */
        SetCharAsMissionChar(m_Target);
        SetBlockingOfNonTemporaryEvents(m_Target, true);

        /*
         * Immediately enter ragdoll.
         *
         * 1800 ms = 1.8 seconds.
         */
        SwitchPedToRagdoll(
            m_Target,
            0,
            1800,
            1,
            1,
            0,
            0
        );

        m_RagdollEndTime = GetTickCount() + 1800;

        /*
         * Niko remains frozen.
         */
        FreezeCharPosition(m_Niko, true);
        SetPlayerControl(m_Player, false);

        /*
         * Camera follows possessed ped.
         */
        CreateCamera();

        if (m_CameraCreated)
        {
            SetCamTargetPed(m_Camera, m_Target);
            SetCamActive(m_Camera, true);
            SetCamPropagate(m_Camera, true);
            SetCamFov(m_Camera, 65.0f);
        }

        m_Mode = MODE_POSSESSED;
    }

    void UpdatePossessed()
    {
        if (!DoesCharExist(m_Target))
        {
            ReturnToNiko();
            return;
        }

        if (IsCharDead(m_Target))
        {
            ReturnToNiko();
            return;
        }

        /*
         * Wait until the 1.8 second possession/ragdoll animation
         * has completed.
         */
        if (GetTickCount() < m_RagdollEndTime)
            return;

        /*
         * Make the ped controllable again.
         */
        SwitchPedToAnimated(m_Target, true);

        /*
         * WASD movement.
         */
        f32 x;
        f32 y;
        f32 z;

        GetCharCoordinates(m_Target, &x, &y, &z);

        f32 camPitch;
        f32 camRoll;
        f32 camYaw;

        camYaw = 0.0f;

        if (m_CameraCreated && DoesCamExist(m_Camera))
        {
            GetCamRot(
                m_Camera,
                &camPitch,
                &camRoll,
                &camYaw
            );
        }

        const f32 DEG_TO_RAD =
            3.14159265358979323846f / 180.0f;

        f32 yaw = camYaw * DEG_TO_RAD;

        /*
         * GTA heading:
         * forward = (-sin(yaw), cos(yaw))
         */
        f32 forwardX = -std::sin(yaw);
        f32 forwardY =  std::cos(yaw);

        f32 rightX = std::cos(yaw);
        f32 rightY = std::sin(yaw);

        f32 moveX = 0.0f;
        f32 moveY = 0.0f;

        if (GetAsyncKeyState('W') & 0x8000)
        {
            moveX += forwardX;
            moveY += forwardY;
        }

        if (GetAsyncKeyState('S') & 0x8000)
        {
            moveX -= forwardX;
            moveY -= forwardY;
        }

        if (GetAsyncKeyState('D') & 0x8000)
        {
            moveX += rightX;
            moveY += rightY;
        }

        if (GetAsyncKeyState('A') & 0x8000)
        {
            moveX -= rightX;
            moveY -= rightY;
        }

        f32 length =
            std::sqrt(
                moveX * moveX +
                moveY * moveY
            );

        if (length > 0.001f)
        {
            moveX /= length;
            moveY /= length;

            /*
             * Normal speed.
             */
            f32 speed = 0.055f;

            /*
             * Shift = run.
             */
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
            {
                speed = 0.105f;
            }

            x += moveX * speed;
            y += moveY * speed;

            SetCharCoordinates(
                m_Target,
                x,
                y,
                z
            );

            /*
             * Face movement direction.
             */
            f32 heading;

            GetHeadingFromVector2D(
                moveX,
                moveY,
                &heading
            );

            SetCharHeading(
                m_Target,
                heading
            );
        }
    }

    void LeavePossessedToSpirit()
    {
        if (DoesCharExist(m_Target))
        {
            SwitchPedToAnimated(m_Target, true);
            SetBlockingOfNonTemporaryEvents(
                m_Target,
                false
            );

            MarkCharAsNoLongerNeeded(&m_Target);
        }

        m_Target = {};

        m_Mode = MODE_SPIRIT;

        FreezeCharPosition(m_Niko, true);
        SetPlayerControl(m_Player, false);

        f32 x;
        f32 y;
        f32 z;
        f32 heading;

        GetCharCoordinates(
            m_Niko,
            &x,
            &y,
            &z
        );

        GetCharHeading(
            m_Niko,
            &heading
        );

        m_SpiritYaw = heading;
        m_SpiritPitch = -25.0f;

        CreateCamera();

        UpdateSpiritCamera(
            x,
            y,
            z
        );
    }

    void ReturnToNiko()
    {
        /*
         * Release possessed NPC.
         */
        if (DoesCharExist(m_Target))
        {
            SwitchPedToAnimated(m_Target, true);

            SetBlockingOfNonTemporaryEvents(
                m_Target,
                false
            );

            MarkCharAsNoLongerNeeded(&m_Target);
        }

        m_Target = {};

        /*
         * Destroy custom camera.
         */
        DestroyCamera();

        /*
         * Restore Niko.
         */
        if (DoesCharExist(m_Niko))
        {
            FreezeCharPosition(
                m_Niko,
                false
            );

            SetCharVisible(
                m_Niko,
                true
            );

            ClearCharTasksImmediately(
                m_Niko
            );

            SetCamBehindPed(
                m_Niko
            );
        }

        SetCameraControlsDisabledWithPlayerControls(false);

        SetPlayerControl(
            m_Player,
            true
        );

        m_Mode = MODE_NIKO;
    }

    void RestoreEverything()
    {
        if (DoesCharExist(m_Target))
        {
            SwitchPedToAnimated(m_Target, true);

            SetBlockingOfNonTemporaryEvents(
                m_Target,
                false
            );

            MarkCharAsNoLongerNeeded(
                &m_Target
            );
        }

        m_Target = {};

        DestroyCamera();

        if (DoesCharExist(m_Niko))
        {
            FreezeCharPosition(
                m_Niko,
                false
            );

            SetCharVisible(
                m_Niko,
                true
            );
        }

        if (m_Player != 0)
        {
            SetCameraControlsDisabledWithPlayerControls(false);
            SetPlayerControl(m_Player, true);
        }

        m_Mode = MODE_NIKO;
    }
};


/*
 * This is the only global thread object.
 *
 * No CustomThread.h
 * No CustomThread.cpp
 * No Main.cpp
 */
static TGHThread g_TGHThread;


BOOL APIENTRY DllMain(
    HANDLE hModule,
    DWORD fdwReason,
    LPVOID lpReserved
)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(
            (HMODULE)hModule
        );

        ScriptHookManager::RegisterThread(
            &g_TGHThread
        );

        break;

    case DLL_PROCESS_DETACH:
        break;
    }

    return TRUE;
}
