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

    DWORD m_RagdollEndTime;

    bool m_CameraCreated;

public:

    TGHThread()
    {
        char name[] = "TGHMod";
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
            m_Player =
                ConvertIntToPlayerIndex(
                    GetPlayerId()
                );
        }

        GetPlayerChar(
            m_Player,
            &m_Niko
        );

        if (!DoesCharExist(m_Niko))
            return;

        /*
         * T
         *
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
         * G
         *
         * Spirit -> nearest NPC
         */
        if ((GetAsyncKeyState('G') & 1) != 0)
        {
            if (m_Mode == MODE_SPIRIT)
            {
                PossessNearestPed();
            }
        }

        /*
         * H
         *
         * Spirit / possessed -> Niko
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

    void EnterSpirit()
    {
        m_Mode = MODE_SPIRIT;

        ClearCharTasksImmediately(
            m_Niko
        );

        FreezeCharPosition(
            m_Niko,
            true
        );

        SetPlayerControl(
            m_Player,
            false
        );

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

        CreateSpiritCamera(
            x,
            y,
            z
        );
    }

    void CreateSpiritCamera(
        f32 x,
        f32 y,
        f32 z
    )
    {
        if (!m_CameraCreated)
        {
            m_Camera = {};

            CreateCam(
                14,
                &m_Camera
            );

            if (!DoesCamExist(m_Camera))
                return;

            m_CameraCreated = true;
        }

        UpdateSpiritCamera(
            x,
            y,
            z
        );

        SetCamActive(
            m_Camera,
            true
        );

        SetCamPropagate(
            m_Camera,
            true
        );

        SetCamFov(
            m_Camera,
            65.0f
        );
    }

    void UpdateSpirit()
    {
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

        GetCharCoordinates(
            m_Niko,
            &x,
            &y,
            &z
        );

        UpdateSpiritCamera(
            x,
            y,
            z
        );
    }

    void UpdateSpiritCamera(
        f32 x,
        f32 y,
        f32 z
    )
    {
        if (!m_CameraCreated)
            return;

        const f32 PI =
            3.14159265358979323846f;

        const f32 yaw =
            m_SpiritYaw * PI / 180.0f;

        const f32 distance = 5.0f;
        const f32 height = 4.0f;

        const f32 camX =
            x - std::sin(yaw) * distance;

        const f32 camY =
            y - std::cos(yaw) * distance;

        const f32 camZ =
            z + height;

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
    }

    void PossessNearestPed()
    {
        f32 x;
        f32 y;
        f32 z;

        GetCharCoordinates(
            m_Niko,
            &x,
            &y,
            &z
        );

        Ped nearest = {};

        if (!GetClosestChar(
            x,
            y,
            z,
            4.0f,
            false,
            false,
            &nearest
        ))
        {
            return;
        }

        if (!DoesCharExist(nearest))
            return;

        if (nearest == m_Niko)
            return;

        if (IsCharDead(nearest))
            return;

        m_Target = nearest;

        SetCharAsMissionChar(
            m_Target
        );

        SetBlockingOfNonTemporaryEvents(
            m_Target,
            true
        );

        SwitchPedToRagdoll(
            m_Target,
            0,
            1800,
            1,
            1,
            0,
            0
        );

        m_RagdollEndTime =
            GetTickCount() + 1800;

        FreezeCharPosition(
            m_Niko,
            true
        );

        SetPlayerControl(
            m_Player,
            false
        );

        if (!m_CameraCreated)
        {
            m_Camera = {};

            CreateCam(
                14,
                &m_Camera
            );

            if (DoesCamExist(m_Camera))
            {
                m_CameraCreated = true;
            }
        }

        if (m_CameraCreated)
        {
            SetCamTargetPed(
                m_Camera,
                m_Target
            );

            SetCamActive(
                m_Camera,
                true
            );

            SetCamPropagate(
                m_Camera,
                true
            );
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

        if (GetTickCount() <
            m_RagdollEndTime)
        {
            return;
        }

        SwitchPedToAnimated(
            m_Target,
            true
        );

        f32 x;
        f32 y;
        f32 z;

        GetCharCoordinates(
            m_Target,
            &x,
            &y,
            &z
        );

        f32 pitch = 0.0f;
        f32 roll = 0.0f;
        f32 yaw = 0.0f;

        if (m_CameraCreated &&
            DoesCamExist(m_Camera))
        {
            GetCamRot(
                m_Camera,
                &pitch,
                &roll,
                &yaw
            );
        }

        const f32 PI =
            3.14159265358979323846f;

        const f32 radians =
            yaw * PI / 180.0f;

        f32 forwardX =
            -std::sin(radians);

        f32 forwardY =
             std::cos(radians);

        f32 rightX =
             std::cos(radians);

        f32 rightY =
             std::sin(radians);

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

        if (GetAsyncKeyState('A') & 0x8000)
        {
            moveX -= rightX;
            moveY -= rightY;
        }

        if (GetAsyncKeyState('D') & 0x8000)
        {
            moveX += rightX;
            moveY += rightY;
        }

        const f32 length =
            std::sqrt(
                moveX * moveX +
                moveY * moveY
            );

        if (length <= 0.001f)
            return;

        moveX /= length;
        moveY /= length;

        f32 speed = 0.055f;

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

    void LeavePossessedToSpirit()
    {
        if (DoesCharExist(m_Target))
        {
            SwitchPedToAnimated(
                m_Target,
                true
            );

            SetBlockingOfNonTemporaryEvents(
                m_Target,
                false
            );

            MarkCharAsNoLongerNeeded(
                &m_Target
            );
        }

        m_Target = {};

        m_Mode = MODE_SPIRIT;

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

        CreateSpiritCamera(
            x,
            y,
            z
        );
    }

    void ReturnToNiko()
    {
        if (DoesCharExist(m_Target))
        {
            SwitchPedToAnimated(
                m_Target,
                true
            );

            SetBlockingOfNonTemporaryEvents(
                m_Target,
                false
            );

            MarkCharAsNoLongerNeeded(
                &m_Target
            );
        }

        m_Target = {};

        DestroySpiritCamera();

        if (DoesCharExist(m_Niko))
        {
            FreezeCharPosition(
                m_Niko,
                false
            );

            ClearCharTasksImmediately(
                m_Niko
            );

            SetCharVisible(
                m_Niko,
                true
            );

            SetCamBehindPed(
                m_Niko
            );
        }

        SetPlayerControl(
            m_Player,
            true
        );

        m_Mode = MODE_NIKO;
    }

    void DestroySpiritCamera()
    {
        if (!m_CameraCreated)
            return;

        if (DoesCamExist(m_Camera))
        {
            SetCamActive(
                m_Camera,
                false
            );

            SetCamPropagate(
                m_Camera,
                false
            );

            DestroyCam(
                m_Camera
            );
        }

        m_Camera = {};
        m_CameraCreated = false;
    }

    void RestoreEverything()
    {
        if (DoesCharExist(m_Target))
        {
            SwitchPedToAnimated(
                m_Target,
                true
            );

            SetBlockingOfNonTemporaryEvents(
                m_Target,
                false
            );

            MarkCharAsNoLongerNeeded(
                &m_Target
            );
        }

        m_Target = {};

        DestroySpiritCamera();

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
            SetPlayerControl(
                m_Player,
                true
            );
        }

        m_Mode = MODE_NIKO;
    }
};


/*
 * Deliberately do NOT create a global TGHThread object.
 *
 * The object is created after DLL_PROCESS_ATTACH begins.
 */
static TGHThread *g_TGHThread = 0;


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

        g_TGHThread =
            new TGHThread();

        ScriptHookManager::RegisterThread(
            g_TGHThread
        );

        break;

    case DLL_PROCESS_DETACH:

        g_TGHThread = 0;

        break;
    }

    return TRUE;
}
