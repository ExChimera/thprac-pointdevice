#include "thprac_games.h"
#include "thprac_utils.h"
#include "../../resource.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <list>
#include <format>
#include <d3d9.h>
#include <dinput.h>
#include <imm.h>
#include <wrl/client.h>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace THPrac {
namespace TH14 {
    bool g_show_bullet_hitbox = false;
    enum addrs {
        BOMB_PTR = 0x4DB52C,
        ENEMY_MANAGER_PTR = 0x4db544,
        ITEM_MANAGER_PTR = 0x4db660,
        CHARA_ADDR = 0x4f5828,
        SUBSHOT_ADDR = 0x4f582c,
        BONUS_TOTAL = 0x4f5894,
        PLAYER_PTR = 0x4db67c,
        STAGE_NUM = 0x4f58a4,
        SUPERVISOR_NEXT_STATE = 0x4d964c,
        WINDOW_PTR = 0x4f5a18,
    };
    int g_lock_timer = 0;
    bool g_lock_timer_flag = false;
    static bool pointdeviceEnabled = false;
    static bool pointdeviceRun = false;
    static bool pointdeviceLocked = false;
    static bool pointdeviceKeyHookInstalled = false;

    static constexpr int lifeFragmentItem = 4, bombFragmentItem = 6, cycleRewardItem = 12;
    using SpawnItem = uintptr_t(__thiscall*)(uintptr_t, int, const float*, uint32_t, float, float, int);
    static const auto spawnItem = reinterpret_cast<SpawnItem>(0x4397b0);

    namespace Pointdevice {
        static bool IgnoreRetryKey(WPARAM key);
    }

    struct ChapterCounts {
        int32_t bonus05;
        int32_t bonus08;
        int32_t bonus12;
        int32_t bonus16;
        int32_t bonus20;
        int32_t bombsUsed;
    };
    static ChapterCounts chapterCounts {};

    static LRESULT CALLBACK PointdeviceKeyHook(int code, WPARAM action, LPARAM data)
    {
        if (code == HC_ACTION && action == PM_REMOVE) {
            auto& message = *reinterpret_cast<MSG*>(data);
            const auto gameWindow = GetMemContent<HWND>(WINDOW_PTR);
            constexpr LPARAM wasDown = 1L << 30;
            if ((message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN)
                && message.hwnd == gameWindow && Pointdevice::IgnoreRetryKey(message.wParam)) {
                message.message = WM_NULL;
            }
            if (message.message == WM_KEYDOWN && !(message.lParam & wasDown)
                && !pointdeviceLocked && message.hwnd == gameWindow && GetForegroundWindow() == gameWindow) {
                // Recover IME-processed keys while the original virtual key is
                // still available, before the game calls TranslateMessage.
                auto key = message.wParam;
                if (key == VK_PROCESSKEY) {
                    key = ImmGetVirtualKey(message.hwnd);
                    if (key == VK_PROCESSKEY) {
                        const auto scanCode = (message.lParam >> 16) & 0xff;
                        key = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK);
                    }
                }
                if (key == 'P')
                    pointdeviceEnabled = !pointdeviceEnabled;
            }
        }
        return CallNextHookEx(nullptr, code, action, data);
    }
    
    using std::pair;
    struct THPracParam {
        int32_t mode;
        int32_t stage;
        int32_t section;
        int32_t phase;

        int64_t score;
        int32_t life;
        int32_t life_fragment;
        int32_t bomb;
        int32_t bomb_fragment;
        int32_t cycle;
        int32_t power;
        int32_t value;
        int32_t graze;

        int32_t faketype;

        bool dlg;
        bool keepSpellDrops;

        bool _playLock = false;
        void Reset()
        {
            memset(this, 0, sizeof(THPracParam));
        }
        bool ReadJson(std::string& json)
        {
            ParseJson();

            ForceJsonValue(game, "th14");
            GetJsonValue(mode);
            GetJsonValue(stage);
            GetJsonValue(section);
            GetJsonValue(phase);
            GetJsonValueEx(dlg, Bool);
            GetJsonValueEx(keepSpellDrops, Bool);

            GetJsonValue(score);
            GetJsonValue(life);
            GetJsonValue(life_fragment);
            GetJsonValue(bomb);
            GetJsonValue(bomb_fragment);
            GetJsonValue(cycle);
            GetJsonValue(power);
            GetJsonValue(value);
            GetJsonValue(graze);
            GetJsonValue(faketype);

            return true;
        }
        std::string GetJson()
        {
            if (mode == 1) {
                CreateJson();

                AddJsonValueEx(version, GetVersionStr(), jalloc);
                AddJsonValueEx(game, "th14", jalloc);
                AddJsonValue(mode);
                AddJsonValue(stage);
                if (section)
                    AddJsonValue(section);
                if (phase)
                    AddJsonValue(phase);
                if (dlg)
                    AddJsonValue(dlg) 
                if (keepSpellDrops)
                    AddJsonValue(keepSpellDrops)

                AddJsonValue(score);
                AddJsonValue(life);
                AddJsonValue(life_fragment);
                AddJsonValue(bomb);
                AddJsonValue(bomb_fragment);
                AddJsonValue(cycle);
                AddJsonValue(power);
                AddJsonValue(value);
                AddJsonValue(graze);
                AddJsonValue(faketype);

                ReturnJson();
            } else if (mode == 2) {
                CreateJson();

                AddJsonValueEx(version, GetVersionStr(), jalloc);
                AddJsonValueEx(game, "th14", jalloc);
                AddJsonValue(mode);
                AddJsonValue(phase);

                ReturnJson();
            }

            CreateJson();
            jalloc; // Dummy usage to silence C4189
            ReturnJson();
        }
    };
    THPracParam thPracParam {};

    class THGuiPrac : public Gui::GameGuiWnd {
        THGuiPrac() noexcept
        {
            *mMode = 1;
            *mLife = 8;
            *mBomb = 8;
            *mPower = 400;
            *mValue = 10000;

            SetFade(0.8f, 0.1f);
            SetStyle(ImGuiStyleVar_WindowRounding, 0.0f);
            SetStyle(ImGuiStyleVar_WindowBorderSize, 0.0f);
            OnLocaleChange();
        }
        SINGLETON(THGuiPrac)
    public:

        __declspec(noinline) void State(int state)
        {
            switch (state) {
            case 0:
                break;
            case 1:
                mDiffculty = *((int32_t*)0x4f5834);
                mShotType = *((int32_t*)0x4f582c);
                SetFade(0.8f, 0.1f);
                Open();
                thPracParam.Reset();
            case 2:
                break;
            case 3:
                SetFade(0.8f, 0.1f);
                Close();

                // Fill Param
                thPracParam.mode = *mMode;
                thPracParam.stage = *mStage;
                thPracParam.section = CalcSection();
                thPracParam.phase = SpellPhase() ? *mPhase : 0;
                if (SectionHasDlg(thPracParam.section))
                    thPracParam.dlg = *mDlg;
                if (SectionHasSpellDrops(thPracParam.section))
                    thPracParam.keepSpellDrops = *mKeepSpellDrops;

                thPracParam.score = *mScore;
                thPracParam.life = *mLife;
                thPracParam.life_fragment = *mLifeFragment;
                thPracParam.bomb = *mBomb;
                thPracParam.bomb_fragment = *mBombFragment;
                thPracParam.cycle = *mCycle;
                thPracParam.power = *mPower;
                thPracParam.value = *mValue;
                thPracParam.graze = *mGraze;
                thPracParam.faketype = *mFakeType;
                break;
            case 4:
                Close();
                *mNavFocus = 0;
                break;
            default:
                break;
            }
        }

    protected:
        virtual void OnLocaleChange() override
        {
            SetTitle(S(TH_MENU));
            switch (Gui::LocaleGet()) {
            case Gui::LOCALE_ZH_CN:
                SetSizeRel(0.5f, 0.81f);
                SetPosRel(0.4f, 0.14f);
                SetItemWidthRel(-0.100f);
                SetAutoSpacing(true);
                break;
            case Gui::LOCALE_EN_US:
                SetSizeRel(0.6f, 0.75f);
                SetPosRel(0.35f, 0.165f);
                SetItemWidthRel(-0.100f);
                SetAutoSpacing(true);
                break;
            case Gui::LOCALE_JA_JP:
                SetSizeRel(0.56f, 0.81f);
                SetPosRel(0.37f, 0.14f);
                SetItemWidthRel(-0.105f);
                SetAutoSpacing(true);
                break;
            default:
                break;
            }
        }
        virtual void OnContentUpdate() override
        {
            ImGui::TextUnformatted(S(TH_MENU));
            ImGui::Separator();

            PracticeMenu();
        }
        const th_glossary_t* SpellPhase()
        {
            auto section = CalcSection();
            if (section == TH14_ST6_BOSS9) {
                return TH_SPELL_PHASE1;
            } else if (section == TH14_ST6_BOSS10) {
                return TH14_SPELL_4PHASE;
            } else if (section == TH14_ST7_END_S9) {
                return TH14_SPELL_PHASE_TIMEOUT;
            } else if (section == TH14_ST7_END_S10){
                return TH14_SPELL_5PHASE;
            } else if (section == TH14_ST3_MID2_HL){
                return TH_TIMEOUT_SETTING;
            }
            return nullptr;
        }
        void PracticeMenu()
        {
            mMode();
            if (mStage())
                *mSection = *mChapter = 0;
            if (*mMode == 1) {
                if (mWarp())
                    *mSection = *mChapter = *mPhase = 0;
                if (*mWarp)
                    SectionWidget();
                
                mLife();
                mLifeFragment();
                mBomb();
                mBombFragment();
                mCycle();
                auto power_str = std::to_string((float)(*mPower) / 100.0f).substr(0, 4);
                mPower(power_str.c_str());
                mValue();
                mValue.RoundDown(10);
                mGraze();
                mScore();
                mScore.RoundDown(10);
            }

            mNavFocus();
        }
        int CalcSection()
        {
            int chapterId = 0;
            switch (*mWarp) {
            case 1: // Chapter
                // Chapter Id = 10000 + Stage * 100 + Section
                chapterId += (*mStage + 1) * 100;
                chapterId += *mChapter;
                chapterId += 10000; // Base of chapter ID is 1000.
                return chapterId;
                break;
            case 2:
            case 3: // Mid boss & End boss
                return th_sections_cba[*mStage][*mWarp - 2][*mSection];
                break;
            case 4:
            case 5: // Non-spell & Spellcard
                return th_sections_cbt[*mStage][*mWarp - 4][*mSection];
                break;
            default:
                return 0;
                break;
            }
        }
        bool SectionHasDlg(int32_t section)
        {
            switch (section) {
            case TH14_ST1_BOSS1:
            case TH14_ST2_BOSS1:
            case TH14_ST3_BOSS1:
            case TH14_ST4_BOSS1:
            case TH14_ST5_BOSS1:
            case TH14_ST6_BOSS1:
            case TH14_ST7_END_NS1:
            case TH14_ST7_MID1:
                return true;
            default:
                return false;
            }
        }
        bool SectionHasSpellDrops(int32_t section)
        {
            switch (section) {
            case TH14_ST1_BOSS3:
            case TH14_ST2_BOSS3:
            case TH14_ST3_MID2_EN:
            case TH14_ST3_BOSS3:
            case TH14_ST3_BOSS5:
            case TH14_ST4_MID2:
            case TH14_ST4_BOSS3:
            case TH14_ST4_BOSS5:
            case TH14_ST5_BOSS3:
            case TH14_ST5_BOSS5:
            case TH14_ST5_BOSS7:
            case TH14_ST6_MID2:
            case TH14_ST6_BOSS3:
            case TH14_ST6_BOSS5:
            case TH14_ST6_BOSS7:
            case TH14_ST6_BOSS9:
            case TH14_ST6_BOSS10:
            case TH14_ST7_MID3:
            case TH14_ST7_END_NS2:
            case TH14_ST7_END_NS3:
            case TH14_ST7_END_NS4:
            case TH14_ST7_END_NS5:
            case TH14_ST7_END_NS6:
            case TH14_ST7_END_NS7:
            case TH14_ST7_END_NS8:
            case TH14_ST7_END_S9:
                return true;
            default:
                return false;
            }
        }
        void SectionWidget()
        {
            int st_offset = 0;
            if (*mStage == 3)
            {
                mFakeType();
                if (*mFakeType == 0)
                    st_offset = 4 + mShotType;
                else
                    st_offset = 4 + (*mFakeType == 1 ? 0 : 1);
            }
            static char chapterStr[256] {};
            auto& chapterCounts = mChapterSetup[*mStage];

            switch (*mWarp) {
            case 1: // Chapter
                mChapter.SetBound(1, chapterCounts[0] + chapterCounts[1]);

                if (chapterCounts[1] == 0 && chapterCounts[0] != 0) {
                    sprintf_s(chapterStr, S(TH_STAGE_PORTION_N), *mChapter);
                } else if (*mChapter <= chapterCounts[0]) {
                    sprintf_s(chapterStr, S(TH_STAGE_PORTION_1), *mChapter);
                } else {
                    sprintf_s(chapterStr, S(TH_STAGE_PORTION_2), *mChapter - chapterCounts[0]);
                };

                mChapter(chapterStr);
                break;
            case 2:
            case 3: { // Mid boss & End boss
                const th_sections_t* sections = th_sections_cba[*mStage][*mWarp - 2];
                if (mSection(TH_WARP_SELECT[*mWarp],
                        th_sections_cba[*mStage + st_offset][*mWarp - 2],
                        th_sections_str[::THPrac::Gui::LocaleGet()][mDiffculty]))
                    *mPhase = 0;
                if (SectionHasDlg(sections[*mSection]))
                    mDlg();
                mPhase(TH_PHASE, SpellPhase());

                if (SectionHasSpellDrops(sections[*mSection]))
                    mKeepSpellDrops();
                break;
            }
            case 4:
            case 5: { // Non-spell & Spellcard
                const th_sections_t* sections = th_sections_cbt[*mStage][*mWarp - 4];
                if (mSection(TH_WARP_SELECT[*mWarp],
                        th_sections_cbt[*mStage + st_offset][*mWarp - 4],
                        th_sections_str[::THPrac::Gui::LocaleGet()][mDiffculty]))
                    *mPhase = 0;
                if (SectionHasDlg(sections[*mSection]))
                    mDlg();
                mPhase(TH_PHASE, SpellPhase());

                if (SectionHasSpellDrops(sections[*mSection]))
                    mKeepSpellDrops();
                break;
            }
            default:
                break;
            }
        }

        Gui::GuiCombo mMode { TH_MODE, TH_MODE_SELECT };
        Gui::GuiCombo mStage { TH_STAGE, TH_STAGE_SELECT };
        Gui::GuiCombo mWarp { TH_WARP, TH_WARP_SELECT };
        Gui::GuiCombo mSection { TH_MODE };
        Gui::GuiCombo mPhase { TH_PHASE };
        Gui::GuiCheckBox mDlg { TH_DLG };
        Gui::GuiCheckBox mKeepSpellDrops { TH_DROP_ITEMS };
        Gui::GuiCombo mCycle { TH_CYCLE, TH14_CYCLE_LIST };
        Gui::GuiCombo mFakeType { TH14_FAKE_TYPE, TH14_FAKE_TYPE_LIST };

        Gui::GuiSlider<int, ImGuiDataType_S32> mChapter { TH_CHAPTER, 0, 0 };
        Gui::GuiDrag<int64_t, ImGuiDataType_S64> mScore { TH_SCORE, 0, 9999999990, 10, 100000000 };
        Gui::GuiSlider<int, ImGuiDataType_S32> mLife { TH_LIFE, 0, 8 };
        Gui::GuiSlider<int, ImGuiDataType_S32> mLifeFragment { TH_LIFE_FRAGMENT, 0, 2 };
        Gui::GuiSlider<int, ImGuiDataType_S32> mBomb { TH_BOMB, 0, 8 };
        Gui::GuiSlider<int, ImGuiDataType_S32> mBombFragment { TH_BOMB_FRAGMENT, 0, 7 };
        Gui::GuiSlider<int, ImGuiDataType_S32> mPower { TH_POWER, 100, 400 };
        Gui::GuiDrag<int, ImGuiDataType_S32> mValue { TH_VALUE, 0, 999990, 10, 100000 };
        Gui::GuiDrag<int, ImGuiDataType_S32> mGraze { TH_GRAZE, 0, 999999, 1, 100000 };

        Gui::GuiNavFocus mNavFocus { TH_STAGE, TH_MODE, TH_WARP, TH_DLG,
            TH_MID_STAGE, TH_END_STAGE, TH_NONSPELL, TH_SPELL, TH_PHASE, TH_CHAPTER,
            TH_SCORE, TH_LIFE, TH_LIFE_FRAGMENT, TH_BOMB, TH_BOMB_FRAGMENT, TH_CYCLE,
            TH_POWER, TH_VALUE, TH_GRAZE, TH14_FAKE_TYPE };

        int mChapterSetup[7][2] {
            { 3, 2 },
            { 3, 3 },
            { 3, 3 },
            { 4, 3 },
            { 4, 2 },
            { 4, 0 },
            { 4, 5 },
        };

        int mDiffculty = 0;
        int mShotType = 0;
    };
    class THGuiRep : public Gui::GameGuiWnd {
        THGuiRep() noexcept
        {
            wchar_t appdata[MAX_PATH];
            GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
            mAppdataPath = appdata;
        }
        SINGLETON(THGuiRep)
    public:

        void CheckReplay()
        {
            uint32_t index = GetMemContent(0x4db6a4, 0x5aec);
            char* repName = (char*)GetMemAddr(0x4db6a4, index * 4 + 0x5af4, 0x220);
            std::wstring repDir(mAppdataPath);
            repDir.append(L"\\ShanghaiAlice\\th14\\replay\\");
            repDir.append(mb_to_utf16(repName, 932));

            std::string param;
            if (ReplayLoadParam(repDir.c_str(), param) && mRepParam.ReadJson(param))
                mParamStatus = true;
            else
                mRepParam.Reset();
        }

        bool mRepStatus = false;
        void State(int state)
        {
            switch (state) {
            case 1:
                mRepStatus = false;
                mParamStatus = false;
                thPracParam.Reset();
                break;
            case 2:
                CheckReplay();
                break;
            case 3:
                mRepStatus = true;
                if (mParamStatus)
                    memcpy(&thPracParam, &mRepParam, sizeof(THPracParam));
                break;
            default:
                break;
            }
        }

    protected:
        std::wstring mAppdataPath;
        bool mParamStatus = false;
        THPracParam mRepParam;
    };
    class THOverlay : public Gui::GameGuiWnd {
        THOverlay() noexcept
        {
            SetTitle("Mod Menu");
            SetFade(0.5f, 0.5f);
            SetPos(10.0f, 10.0f);
            SetSize(0.0f, 0.0f);
            SetWndFlag(
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | 0);
            OnLocaleChange();
        }
        SINGLETON(THOverlay)
    public:

    protected:
        virtual void OnLocaleChange() override
        {
            float x_offset_1 = 0.0f;
            float x_offset_2 = 0.0f;
            switch (Gui::LocaleGet()) {
            case Gui::LOCALE_ZH_CN:
                x_offset_1 = 0.1f;
                x_offset_2 = 0.14f;
                break;
            case Gui::LOCALE_EN_US:
                x_offset_1 = 0.1f;
                x_offset_2 = 0.14f;
                break;
            case Gui::LOCALE_JA_JP:
                x_offset_1 = 0.1f;
                x_offset_2 = 0.14f;
                break;
            default:
                break;
            }

            mMenu.SetTextOffsetRel(x_offset_1, x_offset_2);
            mMuteki.SetTextOffsetRel(x_offset_1, x_offset_2);
            mInfLives.SetTextOffsetRel(x_offset_1, x_offset_2);
            mInfBombs.SetTextOffsetRel(x_offset_1, x_offset_2);
            mInfPower.SetTextOffsetRel(x_offset_1, x_offset_2);
            mTimeLock.SetTextOffsetRel(x_offset_1, x_offset_2);
            mAutoBomb.SetTextOffsetRel(x_offset_1, x_offset_2);
            mElBgm.SetTextOffsetRel(x_offset_1, x_offset_2);
            mInGameInfo.SetTextOffsetRel(x_offset_1, x_offset_2);
            mEnemyMuteki.SetTextOffsetRel(x_offset_1, x_offset_2);
        }
        virtual void OnContentUpdate() override
        {
            mMuteki();
            mInfLives();
            mInfBombs();
            mInfPower();
            mTimeLock();
            mAutoBomb();
            mElBgm();
            mInGameInfo();
            mEnemyMuteki();
            const bool enabled = pointdeviceLocked ? pointdeviceRun : pointdeviceEnabled;
            ImVec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
            if (pointdeviceLocked)
                color = { 1.0f, 0.3f, 0.3f, 1.0f };
            else if (enabled)
                color = Gui::GuiHotKey::EnabledTextColor();
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[Gui::LOCALE_ZH_CN]);
            ImGui::TextColored(color, "%s", enabled ? "[P：无欠模式] " : "P：无欠模式");
            ImGui::PopFont();
        }
        virtual void OnPreUpdate() override
        {
            if (!pointdeviceKeyHookInstalled && Gui::KeyboardInputUpdate('P') == 1 && !pointdeviceLocked)
                pointdeviceEnabled = !pointdeviceEnabled;

            if (mMenu(false) && !ImGui::IsAnyItemActive()) {
                if (*mMenu) {
                    Open();
                } else {
                    Close();
                }
            }
        }

        Gui::GuiHotKeyChord mMenu { "ModMenuToggle", "BACKSPACE", Gui::GetBackspaceMenuChord() };

        HOTKEY_DEFINE(mMuteki, TH_MUTEKI, "F1", VK_F1)
        PATCH_HK(0x44F877, "01")
        HOTKEY_ENDDEF();

        HOTKEY_DEFINE(mInfBombs, TH_INFBOMBS, "F3", VK_F3)
        PATCH_HK(0x412173, "90")
        HOTKEY_ENDDEF();

        HOTKEY_DEFINE(mInfPower, TH_INFPOWER, "F4", VK_F4)
        PATCH_HK(0x44DDA5, "58")
        HOTKEY_ENDDEF();


        HOTKEY_DEFINE(mAutoBomb, TH_AUTOBOMB, "F6", VK_F6)
        EHOOK_HK(0x44DEC4, 7, {
            if (GetMemContent(BOMB_PTR, 0x40)) {
                pCtx->Eip = 0x44DFD1;
            } else {
                pCtx->Eip = 0x44DED1;
            }
        })
        HOTKEY_ENDDEF();
    public:
        Gui::GuiHotKey mInfLives { TH_INFLIVES2, "F2", VK_F2,};

        HOTKEY_DEFINE(mTimeLock, TH_TIMELOCK, "F5", VK_F5)
        PATCH_HK(0x41C5DD, "eb"),
        PATCH_HK(0x424ACA, "90"),
        EHOOK_HK(0x483c4f, 4, { // freeze ECL sub time for stage 5's MainLatter
            if (GetMemContent(STAGE_NUM) != 5) return;

            constexpr uint32_t st5MainLatterID = 65;
            const uint32_t eclSubArr = GetMemContent(ENEMY_MANAGER_PTR, 0xcc, 0x8c);
            const uint32_t st5MainLatterStart = GetMemContent(eclSubArr + 0x8 * st5MainLatterID + 0x4);
            const uint32_t st5MainLatterEnd = GetMemContent(eclSubArr + 0x8 * (st5MainLatterID + 1) + 0x4);
            const uint32_t curInstr = *(uint32_t*)(pCtx->Edi + 0x4);

            // note: this bounds check technique does not work for every sub due to how subs
            //       are stored (not strictly contiguous) but should work for MainLatter subs
            if (curInstr >= st5MainLatterStart && curInstr < st5MainLatterEnd) {
                const bool bossExists = (bool)GetMemContent(ENEMY_MANAGER_PTR, 0x5c);

                if (bossExists) // skip increasing sub time
                    pCtx->Eip = 0x483c53;
            }
        })
        HOTKEY_ENDDEF();

        Gui::GuiHotKey mElBgm { TH_EL_BGM, "F7", VK_F7 };
        Gui::GuiHotKey mInGameInfo {  THPRAC_INGAMEINFO, "F8",  VK_F8, };

        
        HOTKEY_DEFINE(mEnemyMuteki, TH_ENEMY_MUTEKI, "U", 'U')
        PATCH_HK(0x4214C6, "9090"),
        PATCH_HK(0x4214BD, "9090")
        HOTKEY_ENDDEF();
    };

    class TH14InGameInfo : public Gui::GameGuiWnd {
        SINGLETON(TH14InGameInfo)
        TH14InGameInfo() noexcept
        {
            SetTitle("igi");
            SetFade(0.9f, 0.9f);
            SetPosRel(890.0f / 1280.0f, 560.0f / 960.0f);
            SetSizeRel(360.0f / 1280.0f, 0.0f);
            SetWndFlag(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | 
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | 0);
            OnLocaleChange();
        }
    public:
        int32_t mMissCount;

        void OnRestart()
        {
            if (pointdeviceRun) {
                ++mRetryCount;
                mRestarting = true;
            }
        }

        void OnGameStart()
        {
            mMissCount = 0;
            if (!pointdeviceRun || !mRestarting)
                mRetryCount = 0;
            chapterCounts = {};
            mRestarting = false;
        }

    private:
        int32_t mRetryCount = 0;
        bool mRestarting = false;

    protected:
        virtual void OnLocaleChange() override
        {
            float x_offset_1 = 0.0f;
            float x_offset_2 = 0.0f;
            switch (Gui::LocaleGet()) {
            case Gui::LOCALE_ZH_CN:
                x_offset_1 = 0.12f;
                x_offset_2 = 0.172f;
                break;
            case Gui::LOCALE_EN_US:
                x_offset_1 = 0.12f;
                x_offset_2 = 0.16f;
                break;
            case Gui::LOCALE_JA_JP:
                x_offset_1 = 0.18f;
                x_offset_2 = 0.235f;
                break;
            default:
                break;
            }
        }

        virtual void OnContentUpdate() override
        {
            byte cur_player_type = static_cast<byte>((*(int32_t*)(0x4f5828))*2 + (*(int32_t*)(0x4f582c)));
            int32_t diff = *((int32_t*)0x4f5834);
            auto diff_pl = std::format("{} ({})", S(IGI_DIFF[diff]), S(IGI_PL_14[cur_player_type]));
            auto diff_pl_sz = ImGui::CalcTextSize(diff_pl.c_str());

            ImGui::SetCursorPosX(ImGui::GetWindowSize().x * 0.5f - diff_pl_sz.x * 0.5f);
            ImGui::Text(diff_pl.c_str());

            

            ImGui::Columns(2);
            float bonus_cnt_space = 0.0f;
            if (g_adv_igi_options.th14_showBonus){
                std::string bonus_cnt = std::format("{}/{}/{}/{}", chapterCounts.bonus05,
                    chapterCounts.bonus08, chapterCounts.bonus12, chapterCounts.bonus16);
                float bonus_cnt_sz = ImGui::CalcTextSize(bonus_cnt.c_str()).x;
                std::string cycle_cnt = S(THPRAC_INGAMEINFO_TH14_BONUS_BOMB);
                cycle_cnt += ' ';
                float cycle_cnt_sz = ImGui::CalcTextSize(cycle_cnt.c_str()).x * 5.5f;
                float widthmx = std::max(cycle_cnt_sz, bonus_cnt_sz);
                float width1 = ImGui::GetColumnWidth(1);
                if (width1 < widthmx)
                    ImGui::SetColumnWidth(0, ImGui::GetWindowContentRegionWidth() - widthmx);

                float sz_space = ImGui::CalcTextSize("        ").x;
                bonus_cnt_space = sz_space - bonus_cnt_sz * 0.5f;
                if (bonus_cnt_space < 0.0f)
                    bonus_cnt_space = 0.0f;
            }
            


            ImGui::TextUnformatted(pointdeviceRun ? "retry:" : S(THPRAC_INGAMEINFO_MISS_COUNT));
            ImGui::NextColumn();
            ImGui::Text("%8d", pointdeviceRun ? mRetryCount : mMissCount);
            ImGui::NextColumn();
            ImGui::Text(S(THPRAC_INGAMEINFO_BOMB_COUNT));
            ImGui::NextColumn();
            ImGui::Text("%8d", chapterCounts.bombsUsed);
            if (g_adv_igi_options.th14_showItemsCount)
            {
                int n = 0;
                DWORD pitems = *(DWORD*)(0x4DB660);
                if (pitems) {
                    DWORD iter = pitems + 0x14;
                    for (int i = 0; i < 0x1258; i++) {
                        int type = *(DWORD*)(iter + 0xBF4);
                        int movement = *(DWORD*)(iter + 0xBF0);
                        if (movement != 0)
                            if (type ==1 || type==2 || type==3)
                                n++;
                        iter += 0xC18;
                    }
                }
                ImGui::NextColumn();
                ImGui::Text(S(THPRAC_INGAMEINFO_TH14_ITEMS_CNT));
                ImGui::NextColumn();
                ImGui::Text("%8d", n);
            }
            if (g_adv_igi_options.th14_showBonus)
            {
                ImGui::NextColumn();
                ImGui::Text(S(THPRAC_INGAMEINFO_TH14_BONUS_NORMAL));
                ImGui::NextColumn();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + bonus_cnt_space);

                ImGui::TextColored({ 0.6f, 0.90f, 1.0f, 1.0f }, "%d", chapterCounts.bonus05);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::Text("/");
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored({ 0.8f, 1.0f, 0.8f, 1.0f }, "%d", chapterCounts.bonus08);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::Text("/");
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored({ 0.7f, 0.9f, 0.40f, 1.0f }, "%d", chapterCounts.bonus12);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::Text("/");
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored({ 1.0f, 0.85f, 0.5f, 1.0f }, "%d", chapterCounts.bonus16);

                ImGui::NextColumn();
                ImGui::Text(S(THPRAC_INGAMEINFO_TH14_BONUS_20));
                ImGui::NextColumn();
                ImGui::TextColored({ 1.0f, 0.95f, 0.5f, 1.0f }, "%8d", chapterCounts.bonus20);
                ImGui::NextColumn();

                ImGui::Text(S(THPRAC_INGAMEINFO_TH14_BONUS_TOTAL));
                ImGui::NextColumn();
                ImGui::Text("%8d", GetMemContent<int>(BONUS_TOTAL));
                ImGui::NextColumn();

                if (!pointdeviceRun) {
                    const int extends = GetMemContent<int>(0x4f586c);
                    ImGui::Text(S(THPRAC_INGAMEINFO_TH14_EXTEND_COUNT));
                    ImGui::NextColumn();
                    ImGui::TextColored(extends >= 49 ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                        "%8d", extends);
                    ImGui::NextColumn();
                }

                ImGui::Text(S(THPRAC_INGAMEINFO_TH14_BONUS_NEXT));
                ImGui::NextColumn();
                const ImVec4 color_life = { 0.4f, 0.2f, 0.2f, 1.0f };
                const ImVec4 color_bomb = { 0.2f, 0.4f, 0.2f, 1.0f };

                const ImVec4 color_life_next = { 1.0f, 0.6f, 0.6f, 1.0f };
                const ImVec4 color_bomb_next = { 0.5f, 1.0f, 0.5f, 1.0f };
                const int cycle = GetMemContent<int>(BONUS_TOTAL) % 5;
                const char* bombLabel = S(THPRAC_INGAMEINFO_TH14_BONUS_BOMB);
                const char* lifeLabel = S(THPRAC_INGAMEINFO_TH14_BONUS_LIFE);
                for (int i = 0; i < 4; i++){
                    ImGui::TextColored(cycle == i ? color_bomb_next:color_bomb, "%s ", bombLabel);
                    ImGui::SameLine(0.0f, 0.0f);
                }
                if (pointdeviceRun) {
                    const auto& color = cycle == 4 ? color_bomb_next : color_bomb;
                    const auto position = ImGui::GetCursorScreenPos();
                    const auto lifeSize = ImGui::CalcTextSize(lifeLabel);
                    const auto bombSize = ImGui::CalcTextSize(bombLabel);
                    const ImVec2 centered = { position.x + (lifeSize.x - bombSize.x) * 0.5f,
                        position.y + (lifeSize.y - bombSize.y) * 0.5f };
                    const float offset = std::max(1.0f, std::floor(bombSize.y * 0.14f));
                    auto* draw = ImGui::GetWindowDrawList();
                    draw->AddText({ centered.x - offset, centered.y - offset },
                        ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.65f)), bombLabel);
                    draw->AddText({ centered.x + offset, centered.y + offset }, ImGui::GetColorU32(color), bombLabel);
                    ImGui::Dummy({ lifeSize.x, lifeSize.y + offset });
                } else {
                    ImGui::TextColored(cycle == 4 ? color_life_next : color_life, "%s", lifeLabel);
                }
            }
        }

        virtual void OnPreUpdate() override
        {
            if (GetMemContent(PLAYER_PTR)) {
                GameUpdateInner(14);
            } else {
            }
            if (*(THOverlay::singleton().mInGameInfo) && GetMemContent(PLAYER_PTR)) {
                SetPosRel(890.0f / 1280.0f, 560.0f / 960.0f);
                SetSizeRel(360.0f / 1280.0f, 0.0f);
                Open();
            } else {
                Close();
            }
        }

    public:
    };

    class THGuiSP : public Gui::GameGuiWnd {
        THGuiSP() noexcept
        {
            SetFade(0.8f, 0.1f);
            SetStyle(ImGuiStyleVar_WindowRounding, 0.0f);
            SetStyle(ImGuiStyleVar_WindowBorderSize, 0.0f);
            OnLocaleChange();
        }
        SINGLETON(THGuiSP)
    public:

        int mState = 0;
        __declspec(noinline) int State()
        {
            uint32_t spell_id;

            switch (mState) {
            case 0:
                spell_id = GetMemContent(0x4db6a4, GetMemContent(0x4db6a4, 0x28) * 4 + 0x5d70);
                if (mSpellId != spell_id) {
                    *mPhase = 0;
                    mSpellId = spell_id;
                }
                if (!SpellPhase())
                    return 1;
                SetFade(0.8f, 0.1f);
                Open();
                thPracParam.Reset();
                mState = 1;
                break;
            case 1:
                if (IsOpen()) {
                    if (Gui::InGameInputGetConfirm()) {
                        SetFade(0.8f, 0.1f);
                        Close();

                        // Fill Param
                        thPracParam.mode = 2;
                        thPracParam.phase = *mPhase;

                        // Reset Frame Counter
                        *(int32_t*)(*((int32_t*)0x4db6a4) + 0x2b8) = 0;

                        mState = 0;

                        return 1;
                    } else if (Gui::InGameInputGet('X')) {
                        mState = 2;
                    }
                }
                break;
            case 2:
                SetFade(0.8f, 0.1f);
                Close();
                if (IsClosed())
                    mState = 0;
            default:
                break;
            }

            return 0;
        }

    protected:
        virtual void OnLocaleChange() override
        {
            SetTitle(S(TH_SPELL_PRAC));
            switch (Gui::LocaleGet()) {
            case Gui::LOCALE_ZH_CN:
                SetSizeRel(0.38f, 0.12f);
                SetPosRel(0.35f, 0.45f);
                SetItemWidthRel(-0.075f);
                SetAutoSpacing(true);
                break;
            case Gui::LOCALE_EN_US:
                SetSizeRel(0.38f, 0.12f);
                SetPosRel(0.35f, 0.45f);
                SetItemWidthRel(-0.075f);
                SetAutoSpacing(true);
                break;
            case Gui::LOCALE_JA_JP:
                SetSizeRel(0.38f, 0.12f);
                SetPosRel(0.35f, 0.45f);
                SetItemWidthRel(-0.075f);
                SetAutoSpacing(true);
                break;
            default:
                break;
            }
        }
        virtual void OnContentUpdate() override
        {
            ImGui::TextUnformatted(S(TH_SPELL_PRAC));
            ImGui::Separator();

            PracticeMenu();
        }
        const th_glossary_t* SpellPhase()
        {
            if (mSpellId >= 100 && mSpellId <= 103) {
                return TH_SPELL_PHASE1;
            } else if (mSpellId >= 104 && mSpellId <= 107) {
                return TH14_SPELL_4PHASE;
            } else if (mSpellId == 119) {
                return TH14_SPELL_5PHASE;
            }
            return nullptr;
        }
        void PracticeMenu()
        {
            mPhase(TH_PHASE, SpellPhase());
            mNavFocus();
        }

        unsigned int mSpellId = UINT_MAX;

        Gui::GuiCombo mPhase { TH_PHASE };
        Gui::GuiNavFocus mNavFocus { TH_PHASE };
    };

    class THMarisaLaser {
        friend class THAdvOptWnd;
        THMarisaLaser() = default;
        SINGLETON(THMarisaLaser)

    public:
        struct record_t {
            uint32_t stage;
            uint32_t frame;
            uint32_t ordinal;
            float value;
            int fix;

            record_t()
                : stage(0)
                , frame(0)
                , ordinal(0)
                , value(0.0f)
                , fix(0)
            {
            }
            record_t(float _value)
            {
                stage = *(uint32_t*)0x4f58a4;
                frame = *(uint32_t*)0x4f58b0;
                ordinal = 0;
                value = _value;
                fix = 0;
            }
            record_t(float _value, record_t& last)
            {
                stage = *(uint32_t*)0x4f58a4;
                frame = *(uint32_t*)0x4f58b0;
                if (stage == last.stage && frame == last.frame)
                    ordinal = last.ordinal + 1;
                else
                    ordinal = 0;
                value = _value;
                fix = 0;
            }
            void Clear()
            {
                stage = frame = ordinal = fix = 0;
                value = 0.0f;
            }
            bool operator==(const record_t& b) const
            {
                return stage == b.stage && frame == b.frame && ordinal == b.ordinal;
            }
            bool operator<(const record_t& b) const
            {
                if (stage < b.stage)
                    return true;
                else if (stage == b.stage) {
                    if (frame < b.frame)
                        return true;
                    else if (frame == b.frame) {
                        if (ordinal < b.ordinal)
                            return true;
                    }
                }
                return false;
            }
            record_t& operator=(record_t& b)
            {
                stage = b.stage;
                frame = b.frame;
                ordinal = b.ordinal;
                value = b.value;
                fix = b.fix;
                return *this;
            }
        };

        bool mOngoing = false;
        int mState = 0;
        std::list<record_t> mRecordsNormal;
        std::list<record_t> mRecordsPlayback;
        std::list<record_t> mRecordsRecover;
        record_t mPrevRecord {};

        void MergePbAndRec()
        {
            if (mRecordsPlayback.size()) {
                for (auto& record : mRecordsPlayback) {
                    auto it = mRecordsRecover.begin();
                    for (; true; ++it) {
                        if (it == mRecordsRecover.end() || record < *it) {
                            mRecordsRecover.insert(it, record);
                            break;
                        } else if (record == *it)
                            break;
                    }
                }
            }
        }
        float GetAltValue(record_t& record, float fallback_value)
        {
            uint32_t value = 0;
            float fvalue;
            int tempFix = record.fix & 0x0000ffff;
            int tempE = (record.fix & 0xffff0000) >> 16;
            switch (TH14_CORRECTION[tempFix]) {
            case TH14_CORRECTION_RECORDED:
                return record.value;
                break;
            case TH14_CORRECTION_ZERO:
                return 0.0f;
                break;
            case TH14_CORRECTION_E_POSITIVE:
                fvalue = 1.0f;
                for (int i = 0; i < tempE; ++i)
                    fvalue *= 10.0f;
                return fvalue;
                break;
            case TH14_CORRECTION_E_NEGATIVE:
                fvalue = -1.0f;
                for (int i = 0; i < tempE; ++i)
                    fvalue *= 10.0f;
                return fvalue;
                break;
            case TH14_CORRECTION_NAN:
                value = 0x7f800001;
                return *(float*)(&value);
                break;
            case TH14_CORRECTION_NAN_SIGNED:
                value = 0xff800001;
                return *(float*)(&value);
                break;
            case TH14_CORRECTION_INF:
                value = 0x7f800000;
                return *(float*)(&value);
                break;
            case TH14_CORRECTION_NINF:
                value = 0xff800000;
                return *(float*)(&value);
                break;
            case TH14_CORRECTION_SN:
                value = 0x00400000;
                return *(float*)(&value);
                break;
            case TH14_CORRECTION_SN_SIGNED:
                value = 0x80400000;
                return *(float*)(&value);
                break;
            default:
                return fallback_value;
            }
        }
        float AccessNormal(record_t& current)
        {
            mRecordsNormal.push_back(current);
            return current.value;
        }
        float AccessPlayback(record_t& current)
        {
            for (auto it = mRecordsPlayback.begin();
                 it != mRecordsPlayback.end();
                 ++it) {
                if (*it == current)
                    return GetAltValue(*it, current.value);
            }
            return current.value;
        }
        float AccessRecover(record_t& current)
        {
            auto it = mRecordsRecover.begin();
            for (; true; ++it) {
                if (it == mRecordsRecover.end() || current < *it) {
                    mRecordsRecover.insert(it, current);
                    return current.value;
                } else if (current == *it) {
                    return GetAltValue(*it, current.value);
                }
            }
            return current.value;
        }

    public:
        __declspec(noinline) uint32_t Access(uint32_t value)
        {
            float _value = *(float*)(&value);
            record_t current(_value, mPrevRecord);
            mPrevRecord = current;

            switch (mState) {
            case 1:
                _value = AccessNormal(current);
                break;
            case 2:
                _value = AccessPlayback(current);
                break;
            case 3:
                _value = AccessRecover(current);
                break;
            default:
                break;
            }

            return *(uint32_t*)(&_value);
        }
    };

    EHOOK_ST(th14_marisa_laser, 0x45286f, 6, {
        if (g_adv_igi_options.th14_fixMarisaBug) {
            *(uint32_t*)(pCtx->Esp + 0x2c) = 0;
        }
        *(uint32_t*)(pCtx->Esp + 0x2c) = THMarisaLaser::singleton().Access(*(uint32_t*)(pCtx->Esp + 0x2c));
    });
    PATCH_ST(th14_all_clear_bonus_1, 0x43708a, "EB0CCCCCCC");
    EHOOK_ST(th14_all_clear_bonus_2, 0x43712c, 7, {
        *(int32_t*)(GetMemAddr(0x4db550, 0x140)) = *(int32_t*)(0x4f5830);
        if (GetMemContent(0x4f58b8) & 0x10) {
            typedef void (*PScoreFunc)();
            PScoreFunc a = (PScoreFunc)0x449f20;
            a();
            pCtx->Eip = 0x43708f;
        }
    });
    EHOOK_ST(th14_all_clear_bonus_3, 0x43719f, 7, {
        *(int32_t*)(GetMemAddr(0x4db550, 0x140)) = *(int32_t*)(0x4f5830);
        if (GetMemContent(0x4f58b8) & 0x10) {
            *(int32_t*)(GetMemAddr(0x4db550, 0x140)) = *(int32_t*)(0x4f5830);
            typedef void (*PScoreFunc)();
            PScoreFunc a = (PScoreFunc)0x449f20;
            a();
            pCtx->Eip = 0x43708f;
        }
    });
    HOOKSET_DEFINE(th14_master_disable)
    PATCH_DY(th14_master_disable1a, 0x41CAD9, "eb")
    PATCH_DY(th14_master_disable1b, 0x41CAB4, "eb")
    PATCH_DY(th14_master_disable1c, 0x41CA0F, "00")
    HOOKSET_ENDDEF()

    float g_bossMoveDownRange = BOSS_MOVE_DOWN_RANGE_INIT;
    EHOOK_ST(th14_bossmovedown, 0x0042725B,5,{
        float* y_pos = (float*)(pCtx->Edi + 0x3F64);
        float* y_range = (float*)(pCtx->Edi + 0x3F6C);
        float y_max = (*y_pos) + (*y_range) * 0.5f;
        float y_min2 = y_max - (*y_range) * (1.0f - g_bossMoveDownRange);
        *y_pos = (y_max + y_min2) * 0.5f;
        *y_range = (y_max - y_min2);
    });

    class THAdvOptWnd : public Gui::PPGuiWnd {
        SINGLETON(THAdvOptWnd)
    public:
        bool forceBossMoveDown = false;
    private:
        // Option Related Functions
        void MasterDisableInit()
        {
            for (int i = 0; i < 3; i++)
                th14_master_disable[i].Setup();
            for (int i = 0; i < 3; i++)
                th14_master_disable[i].Toggle(g_adv_igi_options.disable_master_autoly);
        }
        void FpsInit()
        {
            if ((mOptCtx.vpatch_base = (uintptr_t)GetModuleHandleW(L"openinputlagpatch.dll")) != NULL) {
                OILPInit(mOptCtx);
            } else {
                mOptCtx.vpatch_base = (int32_t)GetModuleHandleW(L"vpatch_th14.dll");
                if (mOptCtx.vpatch_base) {
                    uint64_t hash[2];
                    CalcFileHash(L"vpatch_th14.dll", hash);

                    bool vp_valid = hash[0] == 16763243947833835441ll && hash[1] == 14013686233300952408ll;
                    if (hash[0] == 5864489015760801383ll && hash[1] == 8525349857717864816ll) {
                        vp_valid = true;
                        if (MessageBoxW(
                                *(HWND*)WINDOW_PTR,
                                L"Old version of vpatch detected. Do you want to download the newest version?",
                                L"thprac: warning",
                                MB_ICONWARNING | MB_YESNO)
                            == IDYES) {
                            ShellExecuteW(NULL, NULL, L"https://maribelhearn.com/mirror/VsyncPatch.zip", NULL, NULL, SW_SHOW);
                        }
                    }

                    if (vp_valid && *(int32_t*)(mOptCtx.vpatch_base + 0x42fbc) == 0) {
                        mOptCtx.fps_status = 2;
                        mOptCtx.fps = *(int32_t*)(mOptCtx.vpatch_base + 0x42fcc);
                    } else if (!vp_valid) {
                        mOptCtx.fps_status = -1;
                    }
                } else if (*(uint8_t*)0x4d9159 == 3) {
                    mOptCtx.fps_status = 1;

                    DWORD oldProtect;
                    VirtualProtect((void*)0x46a792, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
                    *(double**)0x46a792 = &mOptCtx.fps_dbl;
                    VirtualProtect((void*)0x46a792, 4, oldProtect, &oldProtect);
                } else
                    mOptCtx.fps_status = 0;
            }
        }
        void FpsSet()
        {
            if (mOptCtx.fps_status == 3) {
                mOptCtx.oilp_set_game_fps(mOptCtx.fps);
                mOptCtx.oilp_set_replay_skip_fps(mOptCtx.fps_replay_fast);
                mOptCtx.oilp_set_replay_slow_fps(mOptCtx.fps_replay_slow);
            } else if (mOptCtx.fps_status == 1) {
                mOptCtx.fps_dbl = 1.0 / (double)mOptCtx.fps;
            } else if (mOptCtx.fps_status == 2) {
                *(int32_t*)(mOptCtx.vpatch_base + 0x40a34) = mOptCtx.fps;
                *(int32_t*)(mOptCtx.vpatch_base + 0x42fcc) = mOptCtx.fps;
            }
        }
        void GameplayInit()
        {
            th14_all_clear_bonus_1.Setup();
            th14_all_clear_bonus_2.Setup();
            th14_all_clear_bonus_3.Setup();
        }
        void GameplaySet()
        {
            th14_all_clear_bonus_1.Toggle(mOptCtx.all_clear_bonus);
            th14_all_clear_bonus_2.Toggle(mOptCtx.all_clear_bonus);
            th14_all_clear_bonus_3.Toggle(mOptCtx.all_clear_bonus);
        }

        void MarisaLaserInit()
        {
            THMarisaLaser::singleton();
            THMarisaLaser::singleton().mState = g_adv_igi_options.th14_laserRepRepair ? 1 : 0;

            th14_marisa_laser.Setup();

            wchar_t appdata[MAX_PATH];
            GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
            mRepDir = appdata;
            mRepDir += L"\\ShanghaiAlice\\th14\\replay\\";
        }
        bool MarisaLaserMenu()
        {
            auto thMarisaLaser = &THMarisaLaser::singleton();
            bool wndFocus = true;
            ImGui::PushTextWrapPos();
            ImGui::PushItemWidth(GetRelWidth(0.25f));
            if (mLock)
                ImGui::BeginDisabled();
            ImGui::ComboSectionsDefault(S(TH14_MODE), &(thMarisaLaser->mState), TH14_MODE_COMBO, Gui::LocaleGetCurrentGlossary(), "");
            if (ImGui::IsPopupOpen(S(TH14_MODE))) {
                wndFocus = false;
            }
            ImGui::PopItemWidth();
            if (mLock) {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted(S(TH14_LOCKED));
            }

            if (thMarisaLaser->mState == 0) {
                ImGui::TextUnformatted(S(TH14_MODE_NONE_DESC));
            } else if (thMarisaLaser->mState == 1) {
                ImGui::TextUnformatted(S(TH14_MODE_NORMAL_DESC));
            } else if (thMarisaLaser->mState == 2) {
                ImGui::TextUnformatted(S(TH14_MODE_PLAYBACK_DESC));
                if (mRepName[0]) {
                    ImGui::Text(S(TH_REPFIX_SELECTED), mRepName);
                    if (thMarisaLaser->mRecordsPlayback.size()) {
                        int i = 1;
                        for (auto& record : thMarisaLaser->mRecordsPlayback) {
                            int tempFix = record.fix & 0x0000ffff;
                            int tempE = (record.fix & 0xffff0000) >> 16;

                            if (TH14_CORRECTION[tempFix] == TH14_CORRECTION_E_POSITIVE || TH14_CORRECTION[tempFix] == TH14_CORRECTION_E_NEGATIVE) {
                                ImGui::Text(S(TH14_RECORD_E), i++, record.stage, record.frame, *(uint32_t*)&(record.value),
                                    TH14_CORRECTION[tempFix] == TH14_CORRECTION_E_POSITIVE ? "" : "-", tempE);
                            } else {
                                ImGui::Text(S(TH14_RECORD), i++, record.stage, record.frame, *(uint32_t*)&(record.value));
                                ImGui::SameLine(0.0f, 0.0f);
                                ImGui::TextUnformatted(S(TH14_CORRECTION[record.fix]));
                            }
                        }
                    } else {
                        ImGui::TextUnformatted(S(TH14_REPLAY_NO_RECORDS));
                    }
                } else {
                    ImGui::TextUnformatted(S(TH_REPFIX_SELECTED_NONE));
                }
                // TODO: Replay info
            } else if (thMarisaLaser->mState == 3) {
                ImGui::TextUnformatted(S(TH14_MODE_RECOVER_DESC));

                if (ImGui::Button(S(TH_REPFIX_SAVE))) {
                    if (mRepName[0]) {
                        std::wstring rep;
                        rep = mRepDir;
                        rep += mRepName;
                        SaveReplayInternal(rep.c_str());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(S(TH14_LOAD)) && mRepName[0])
                    thMarisaLaser->MergePbAndRec();
                ImGui::SameLine();
                if (ImGui::Button(S(TH14_CLEAR_RECORDS)))
                    thMarisaLaser->mRecordsRecover.clear();
                ImGui::SameLine();
                if (mRepName[0])
                    ImGui::Text(S(TH_REPFIX_SELECTED), mRepName);
                else
                    ImGui::TextUnformatted(S(TH_REPFIX_SELECTED_NONE));

                ImGui::Separator();
                auto it = thMarisaLaser->mRecordsRecover.begin();
                if (it == thMarisaLaser->mRecordsRecover.end())
                    ImGui::TextUnformatted(S(TH14_NO_RECORDS));
                else {
                    int i = 1;
                    for (; it != thMarisaLaser->mRecordsRecover.end(); ++i) {
                        ImGui::Text(S(TH14_RECORD), i, it->stage, it->frame, *(uint32_t*)&(it->value));

                        int tempFix = it->fix & 0x0000ffff;
                        int tempE = (it->fix & 0xffff0000) >> 16;

                        ImGui::SameLine();
                        auto needInputBox = TH14_CORRECTION[tempFix] == TH14_CORRECTION_E_POSITIVE || TH14_CORRECTION[tempFix] == TH14_CORRECTION_E_NEGATIVE;
                        if (needInputBox)
                            ImGui::PushItemWidth(GetRelWidth(0.13f));
                        else
                            ImGui::PushItemWidth(GetRelWidth(0.27f));
                        sprintf_s(mTempStr, "##%s_%d", S(TH14_CORRECTION_VALUE), i);
                        ImGui::ComboSectionsDefault(mTempStr, &tempFix, TH14_CORRECTION, Gui::LocaleGetCurrentGlossary(), "");
                        if (ImGui::IsPopupOpen(mTempStr)) {
                            wndFocus = false;
                        }
                        if (needInputBox) {
                            ImGui::SameLine();
                            sprintf_s(mTempStr, "##e_%d", i);
                            ImGui::InputInt(mTempStr, &tempE, 1, 1, ImGuiInputTextFlags_CharsDecimal);
                            if (tempE > 38)
                                tempE = 38;
                            else if (tempE < 0)
                                tempE = 0;
                        }
                        it->fix = tempFix | (tempE << 16);
                        ImGui::PopItemWidth();

                        ImGui::SameLine();
                        sprintf_s(mTempStr, "%s##%d", S(TH14_DELETE), i);
                        if (ImGui::Button(mTempStr))
                            it = thMarisaLaser->mRecordsRecover.erase(it);
                        else
                            ++it;
                    }
                }
            }
            ImGui::PopTextWrapPos();

            return wndFocus;
        }
        bool LoadReplayInternal(const wchar_t* rep_path)
        {
            auto thMarisaLaser = &THMarisaLaser::singleton();
            DWORD repMagic, bytesRead;
            auto repFile = CreateFileW(rep_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (repFile == INVALID_HANDLE_VALUE)
                return false;
            defer(CloseHandle(repFile));

            SetFilePointer(repFile, 0, nullptr, FILE_BEGIN);

            if (ReadFile(repFile, &repMagic, 4, &bytesRead, nullptr) && bytesRead == 4) {
                DWORD userPtr, userMagic, userLength, userNo;
                SetFilePointer(repFile, 12, nullptr, FILE_BEGIN);
                if (ReadFile(repFile, &userPtr, 4, &bytesRead, nullptr)  && bytesRead == 4) {
                    SetFilePointer(repFile, userPtr, nullptr, FILE_BEGIN);
                    while (true) {
                        if (!ReadFile(repFile, &userMagic, 4, &bytesRead, nullptr) || bytesRead != 4 || userMagic != 'RESU')
                            break;
                        if (!ReadFile(repFile, &userLength, 4, &bytesRead, nullptr) || bytesRead != 4)
                            break;
                        if (!ReadFile(repFile, &userNo, 4, &bytesRead, nullptr) || bytesRead != 4)
                            break;

                        if (userNo == 'RCER') {
                            if (userLength - 12 > 0 && (userLength - 12) % sizeof(THMarisaLaser::record_t) == 0) {
                                void* dataBuffer = malloc(userLength - 12);
                                if (!dataBuffer)
                                    return false;
                                defer(free(dataBuffer));
                                memset(dataBuffer, 0, userLength - 12);
                                if (ReadFile(repFile, dataBuffer, userLength - 12, &bytesRead, nullptr) && (bytesRead == userLength - 12)) {
                                    THMarisaLaser::record_t* p_rec = (THMarisaLaser::record_t*)dataBuffer;
                                    size_t count = (userLength - 12) / sizeof(THMarisaLaser::record_t);
                                    thMarisaLaser->mRecordsPlayback.clear();
                                    for (size_t i = 0; i < count; ++i, ++p_rec)
                                        thMarisaLaser->mRecordsPlayback.push_back(*p_rec);
                                }
                                return bytesRead == userLength - 12;
                            }
                        } else {
                            SetFilePointer(repFile, userLength - 12, nullptr, FILE_CURRENT);
                        }
                    }
                }
            }

            return false;
        }
        bool SaveReplayInternal(const wchar_t* rep_path)
        {
            auto thMarisaLaser = &THMarisaLaser::singleton();
            DWORD bytesProcessed;
            auto repFile = CreateFileW(rep_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (repFile == INVALID_HANDLE_VALUE) {
                MsgBox(MB_ICONERROR | MB_OK, S(TH_ERROR), S(TH_REPFIX_SAVE_ERROR_SRC), nullptr, *(HWND*)WINDOW_PTR);
                return false;
            }
            auto repSize = GetFileSize(repFile, nullptr);
            auto repBuffer = malloc(repSize);
            if (!repBuffer) {
                CloseHandle(repFile);
                return false;
            }
            if (!ReadFile(repFile, repBuffer, repSize, &bytesProcessed, nullptr)) {
                CloseHandle(repFile);
                free(repBuffer);
                return false;
            }
            CloseHandle(repFile);

            std::list<THMarisaLaser::record_t>* records = (thMarisaLaser->mState == 3) ? &thMarisaLaser->mRecordsRecover : &thMarisaLaser->mRecordsNormal;
            auto dataSize = records->size() * sizeof(THMarisaLaser::record_t) + 12;
            auto dataBuffer = malloc(dataSize);
            if (!dataBuffer)
                return false;
            defer(free(dataBuffer));
            memset(dataBuffer, 0, dataSize);
            *(uint32_t*)((int)dataBuffer) = 'RESU';
            *(uint32_t*)((int)dataBuffer + 4) = dataSize;
            *(uint32_t*)((int)dataBuffer + 8) = 'RCER';
            THMarisaLaser::record_t* p_rec = (THMarisaLaser::record_t*)((int)dataBuffer + 12);
            auto i_rec = records->begin();
            for (unsigned int i = 0; i < records->size(); ++i, ++p_rec, ++i_rec)
                *p_rec = *i_rec;

            OPENFILENAMEW ofn;
            wchar_t szFile[MAX_PATH] = L"th14_ud----.rpy";
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = *(HWND*)WINDOW_PTR;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"Replay File\0*.rpy\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrFileTitle = nullptr;
            ofn.nMaxFileTitle = 0;
            ofn.lpstrInitialDir = mRepDir.c_str();
            ofn.lpstrDefExt = L".rpy";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
            if (GetSaveFileNameW(&ofn)) {
                auto outputFile = CreateFileW(szFile, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (outputFile == INVALID_HANDLE_VALUE) {
                    MsgBox(MB_ICONERROR | MB_OK, S(TH_ERROR), S(TH_REPFIX_SAVE_ERROR_DEST), nullptr, ofn.hwndOwner);
                    return false;
                }
                SetFilePointer(outputFile, 0, nullptr, FILE_BEGIN);
                SetEndOfFile(outputFile);
                WriteFile(outputFile, repBuffer, repSize, &bytesProcessed, nullptr);
                if (records->size())
                    WriteFile(outputFile, dataBuffer, dataSize, &bytesProcessed, nullptr);
                CloseHandle(outputFile);

                MsgBox(MB_ICONINFORMATION | MB_OK, S(TH_REPFIX_SAVE_SUCCESS), S(TH_REPFIX_SAVE_SUCCESS_DESC), utf16_to_utf8(szFile).c_str(), ofn.hwndOwner);
            }

            return true;
        }

        THAdvOptWnd() noexcept
        {
            SetWndFlag(ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
            SetFade(0.8f, 0.8f);
            SetStyle(ImGuiStyleVar_WindowRounding, 0.0f);
            SetStyle(ImGuiStyleVar_WindowBorderSize, 0.0f);

            InitUpdFunc([&]() { ContentUpdate(); },
                [&]() { LocaleUpdate(); },
                [&]() {},
                []() {});

            OnLocaleChange();
            FpsInit();
            GameplayInit();
            MarisaLaserInit();
            MasterDisableInit();
            th14_bossmovedown.Setup();
        }
        

    public:
        __declspec(noinline) static bool StaticUpdate()
        {
            auto& advOptWnd = THAdvOptWnd::singleton();

            if (Gui::GetChordPressed(Gui::GetAdvancedMenuChord())) {
                if (advOptWnd.IsOpen())
                    advOptWnd.Close();
                else
                    advOptWnd.Open();
            }
            advOptWnd.Update();

            return advOptWnd.IsOpen();
        }

        __declspec(noinline) void ToggleLock(bool lock)
        {
            auto thMarisaLaser = &THMarisaLaser::singleton();
            mLock = lock;
            th14_marisa_laser.Toggle(lock && thMarisaLaser->mState);
            if (lock) {
                thMarisaLaser->mRecordsNormal.clear();
                thMarisaLaser->mPrevRecord.Clear();
            }
        }
        __declspec(noinline) void SelectReplay()
        {
            uint32_t index = GetMemContent(0x4db6a4, 0x5aec);
            std::wstring repName = mb_to_utf16((char*)GetMemAddr(0x4db6a4, index * 4 + 0x5af4, 0x220), 932);
            wcscpy_s(mRepName, repName.c_str());

            std::wstring rep = mRepDir;
            rep += repName;
            LoadReplayInternal(rep.c_str());
        }
        __declspec(noinline) void DeselectReplay()
        {
            auto thMarisaLaser = &THMarisaLaser::singleton();
            mRepName[0] = 0;
            thMarisaLaser->mRecordsPlayback.clear();
        }
        __declspec(noinline) void SaveReplay(const wchar_t* rep_path)
        {
            auto thMarisaLaser = &THMarisaLaser::singleton();
            if (thMarisaLaser->mState == 1 && *(int32_t*)0x4f5828 == 1 && thMarisaLaser->mRecordsNormal.size())
                SaveReplayInternal(rep_path);
        }

    protected:
        void LocaleUpdate()
        {
            SetTitle("AdvOptMenu");
            switch (Gui::LocaleGet()) {
            case Gui::LOCALE_ZH_CN:
                SetSizeRel(1.0f, 1.0f);
                SetPosRel(0.0f, 0.0f);
                SetItemWidthRel(-0.0f);
                SetAutoSpacing(true);
                break;
            case Gui::LOCALE_EN_US:
                SetSizeRel(1.0f, 1.0f);
                SetPosRel(0.0f, 0.0f);
                SetItemWidthRel(-0.0f);
                SetAutoSpacing(true);
                break;
            case Gui::LOCALE_JA_JP:
                SetSizeRel(1.0f, 1.0f);
                SetPosRel(0.0f, 0.0f);
                SetItemWidthRel(-0.0f);
                SetAutoSpacing(true);
                break;
            default:
                break;
            }
        }
        void ContentUpdate()
        {
            bool wndFocus = true;

            ImGui::TextUnformatted(S(TH_ADV_OPT));
            ImGui::Separator();
            ImGui::BeginChild("Adv. Options", ImVec2(0.0f, 0.0f));

            if (BeginOptGroup<TH_GAME_SPEED>()) {
                if (GameFPSOpt(mOptCtx))
                    FpsSet();
                EndOptGroup();
            }
            if (BeginOptGroup<TH_GAMEPLAY>()) {
                DisableKeyOpt();
                KeyHUDOpt();
                InfLifeOpt();
                ImGui::Checkbox(S(THPRAC_TH14_FORCE_FIX_BUG), &(g_adv_igi_options.th14_fixMarisaBug));
                ImGui::SameLine();
                HelpMarker(S(THPRAC_TH14_FORCE_FIX_BUG_DESC));

                ImGui::Checkbox(S(THPRAC_INGAMEINFO_TH14_SHOW_BONUS), &(g_adv_igi_options.th14_showBonus));
                ImGui::Checkbox(S(THPRAC_INGAMEINFO_TH14_SHOW_ITEMS), &(g_adv_igi_options.th14_showItemsCount));
                ImGui::Checkbox(S(THPRAC_INGAMEINFO_TH14_SHOW_DROP_BAR), &(g_adv_igi_options.th14_showDropBar));
                ImGui::SameLine();
                HelpMarker(S(THPRAC_INGAMEINFO_ADV_DESC1));
                ImGui::SameLine();
                HelpMarker(S(THPRAC_INGAMEINFO_ADV_DESC2));

                if (ImGui::Checkbox(S(TH_BOSS_FORCE_MOVE_DOWN), &forceBossMoveDown)) {
                    th14_bossmovedown.Toggle(forceBossMoveDown);
                }
                ImGui::SameLine();
                HelpMarker(S(TH_BOSS_FORCE_MOVE_DOWN_DESC));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::DragFloat(S(TH_BOSS_FORCE_MOVE_DOWN_RANGE), &g_bossMoveDownRange, 0.002f, 0.0f, 1.0f))
                    g_bossMoveDownRange = std::clamp(g_bossMoveDownRange, 0.0f, 1.0f);
                ImGui::Checkbox(S(THPRAC_SHOW_BULLET_HITBOX), &g_show_bullet_hitbox);
                

                if (ImGui::Checkbox(S(TH_DISABLE_MASTER), &g_adv_igi_options.disable_master_autoly)) {
                    for (int i = 0; i < 3; i++)
                        th14_master_disable[i].Toggle(g_adv_igi_options.disable_master_autoly);
                }
                ImGui::SameLine();
                HelpMarker(S(TH_DISABLE_MASTER_DESC));
                ImGui::Checkbox(S(TH_ENABLE_LOCK_TIMER), &g_adv_igi_options.enable_lock_timer_autoly);

                if (GameplayOpt(mOptCtx))
                    GameplaySet();
                EndOptGroup();
            }
            if (BeginOptGroup<TH14_MARISA_LASER>()) {
                wndFocus &= MarisaLaserMenu();
                EndOptGroup();
            }
            InGameReactionTestOpt();
            AboutOpt("Nylilsa & InceRabbit (for making the video about Marisa's replay desync bug, and for sharing the replay)\n......and you!");
            ImGui::EndChild();
            if (wndFocus)
                ImGui::SetWindowFocus();
        }

        adv_opt_ctx mOptCtx;
        int mMode = 0;
        char mTempStr[128];
        bool mRecording = false;
        bool mLock = false;
        wchar_t mRepName[64];
        std::wstring mRepDir;
        wchar_t _title[256];
        wchar_t _msg[256];
        wchar_t _msg2[256];
    };

    using PracticeEdits = std::vector<std::pair<uint8_t*, uint8_t>>;

    class PracticeECL : public ECLHelper {
    public:
        explicit PracticeECL(void* base, PracticeEdits* edits = nullptr)
            : files(static_cast<uintptr_t*>(base)), edits(edits)
        {
            SetBaseAddr(base);
        }

        void SetFile(unsigned ordinal)
        {
            file = ordinal;
            ECLHelper::SetFile(ordinal);
        }

        template <typename T>
        PracticeECL& operator<<(T value)
        {
            if (edits) {
                auto bytes = reinterpret_cast<uint8_t*>(files[file] + GetPos());
                for (size_t i = 0; i < sizeof(T); ++i)
                    edits->emplace_back(bytes + i, bytes[i]);
            }
            ECLHelper::operator<<(value);
            return *this;
        }

        template <typename Offset, typename T>
        PracticeECL& operator<<(std::pair<Offset, T> edit)
        {
            SetPos(edit.first);
            return *this << edit.second;
        }

    private:
        uintptr_t* files;
        PracticeEdits* edits;
        unsigned file = 0;
    };

    void ECLJump(PracticeECL& ecl, unsigned int start, unsigned int dest, int at_frame, int ecl_time = 0)
    {
        ecl.SetPos(start);
        ecl << ecl_time << 0x0018000C << 0x02ff0000 << 0x00000000 << dest - start << at_frame;
    }
    constexpr unsigned int st7PostMaple = 0x7778;
    __declspec(noinline) void THStageWarp(PracticeECL& ecl, int stage, int portion)
    {
        if (stage == 1) {
            switch (portion) {
            case 1:
                break;
            case 2:
                ECLJump(ecl, 0x689c, 0x6ae8, 60, 90); // 0x6b4c
                ECLJump(ecl, 0x40e4, 0x4118, 0, 0);
                break;
            case 3:
                ECLJump(ecl, 0x689c, 0x6ae8, 60, 90); // 0x6b4c
                ECLJump(ecl, 0x40e4, 0x414c, 0, 0);
                break;
            case 4:
                ECLJump(ecl, 0x689c, 0x6b4c, 60, 90);
                break;
            case 5:
                ECLJump(ecl, 0x689c, 0x6b4c, 60, 90);
                ECLJump(ecl, 0x41e8, 0x4218, 0, 0);
                break;
            default:
                break;
            }
        } else if (stage == 2) {
            switch (portion) {
            case 1:
                break;
            case 2:
                ECLJump(ecl, 0x4f30, 0x51c4, 60, 90); // 0x51e4, 0x5218
                ECLJump(ecl, 0x3004, 0x3038, 0, 0);
                break;
            case 3:
                ECLJump(ecl, 0x4f30, 0x51c4, 60, 90); // 0x51e4, 0x5218
                ECLJump(ecl, 0x3004, 0x306c, 0, 0);
                break;
            case 4:
                ECLJump(ecl, 0x4f30, 0x51e4, 60, 90); // 0x5218
                ECLJump(ecl, 0x31c8, 0x3220, 0, 0);
                break;
            case 5:
                ECLJump(ecl, 0x4f30, 0x5218, 60, 30);
                break;
            case 6:
                ECLJump(ecl, 0x4f30, 0x5218, 60, 90);
                ECLJump(ecl, 0x3118, 0x3160, 0, 0);
                break;
            default:
                break;
            }
        } else if (stage == 3) {
            switch (portion) {
            case 1:
                break;
            case 2:
                ECLJump(ecl, 0x648c, 0x6720, 60, 90); // 0x6740, 0x6774
                ECLJump(ecl, 0x21f4, 0x2228, 0, 0);
                break;
            case 3:
                ECLJump(ecl, 0x648c, 0x6720, 60, 30); // 0x6740, 0x6774
                ECLJump(ecl, 0x21f4, 0x2290, 0, 0);
                break;
            case 4:
                ECLJump(ecl, 0x648c, 0x6740, 60, 90); // 0x6774
                ECLJump(ecl, 0x23c4, 0x241c, 0, 0);
                break;
            case 5:
                ECLJump(ecl, 0x648c, 0x6774, 60, 30);
                break;
            case 6:
                ECLJump(ecl, 0x648c, 0x6774, 60, 90);
                ECLJump(ecl, 0x22f8, 0x233c, 0, 0);
                break;
            default:
                break;
            }
        } else if (stage == 4) {
            switch (portion) {
            case 1:
                break;
            case 2:
                ECLJump(ecl, 0x97f8, 0x9ab4, 60, 90); // 0x9ae4, 0x9b18
                ECLJump(ecl, 0x5090, 0x50c4, 0, 0);
                break;
            case 3:
                ECLJump(ecl, 0x97f8, 0x9ab4, 60, 90); // 0x9ae4, 0x9b18
                ECLJump(ecl, 0x5090, 0x50f8, 0, 0);
                break;
            case 4:
                ECLJump(ecl, 0x97f8, 0x9ab4, 60, 90); // 0x9ae4, 0x9b18
                ECLJump(ecl, 0x5090, 0x512c, 0, 0);
                break;
            case 5:
                ECLJump(ecl, 0x97f8, 0x9ae4, 60, 90); // 0x9b18
                ECLJump(ecl, 0x5240, 0x53c0, 0, 0);
                break;
            case 6:
                ECLJump(ecl, 0x97f8, 0x9b18, 60, 30);
                break;
            case 7:
                ECLJump(ecl, 0x97f8, 0x9b18, 60, 90);
                ECLJump(ecl, 0x5194, 0x51d8, 0, 0);
                break;
            default:
                break;
            }
        } else if (stage == 5) {
            switch (portion) {
            case 1:
                break;
            case 2:
                ECLJump(ecl, 0x8654, 0x8910, 60, 90); // 0x8940, 0x8988
                ECLJump(ecl, 0x2784, 0x27b8, 0, 0);
                break;
            case 3:
                ECLJump(ecl, 0x8654, 0x8910, 60, 90); // 0x8940, 0x8988
                ECLJump(ecl, 0x2784, 0x27ec, 0, 0);
                break;
            case 4:
                ECLJump(ecl, 0x8654, 0x8910, 60, 90); // 0x8940, 0x8988
                ECLJump(ecl, 0x2784, 0x2820, 0, 0);
                break;
            case 5:
                ECLJump(ecl, 0x8654, 0x8988, 60, 90);
                break;
            case 6:
                ECLJump(ecl, 0x8654, 0x8988, 60, 90);
                ECLJump(ecl, 0x2888, 0x28dc, 0, 0);
                break;
            default:
                break;
            }
        } else if (stage == 6) {
            switch (portion) {
            case 1:
                break;
            case 2:
                ECLJump(ecl, 0x79d0, 0x7bb8, 60, 90);
                ECLJump(ecl, 0x2bc0, 0x2bf4, 0, 0);
                break;
            case 3:
                ECLJump(ecl, 0x79d0, 0x7bb8, 60, 90);
                ECLJump(ecl, 0x2bc0, 0x2c28, 0, 0);
                break;
            case 4:
                ECLJump(ecl, 0x79d0, 0x7bb8, 60, 90);
                ECLJump(ecl, 0x2bc0, 0x2c5c, 0, 0);
                break;
            default:
                break;
            }
        } else if (stage == 7) {
            switch (portion) {
            case 1:
                break;
            case 2:
                ECLJump(ecl, st7PostMaple, 0x7a34, 60, 90); // 0x7aac, 0x7acc
                ECLJump(ecl, 0x36ac, 0x36e0, 0, 0);
                break;
            case 3:
                ECLJump(ecl, st7PostMaple, 0x7a34, 60, 50); // 0x7aac, 0x7acc
                ECLJump(ecl, 0x36ac, 0x3714, 0, 0);
                break;
            case 4:
                ECLJump(ecl, st7PostMaple, 0x7a34, 60, 90); // 0x7aac, 0x7acc
                ECLJump(ecl, 0x36ac, 0x3748, 0, 0);
                break;
            case 5:
                ECLJump(ecl, st7PostMaple, 0x7aac, 60, 90); // 0x7acc
                ecl << pair{0x4cf8, 0};
                break;
            case 6:
                ECLJump(ecl, st7PostMaple, 0x7acc, 60, 90);
                break;
            case 7:
                ECLJump(ecl, st7PostMaple, 0x7acc, 60, 10);
                ECLJump(ecl, 0x37b0, 0x37f4, 0, 0);
                break;
            case 8:
                ECLJump(ecl, st7PostMaple, 0x7acc, 60, 90);
                ECLJump(ecl, 0x37b0, 0x3828, 0, 0);
                break;
            case 9:
                ECLJump(ecl, st7PostMaple, 0x7acc, 60, 90);
                ECLJump(ecl, 0x37b0, 0x385c, 0, 0);
                break;
            default:
                break;
            }
        }
    }
    __declspec(noinline) void THPatch(PracticeECL& ecl, th_sections_t section)
    {
        auto st6_boss = [&](int std_id) {
            ECLJump(ecl, 0x79d0, 0x2aa0, 0); // Jump to 630
            ecl << pair{0x2ab0, std_id}; // 630 Param
            ECLJump(ecl, 0x2ab4, 0x7c68, 60); // Jump to MainBoss
            ecl.SetFile(3);
            ecl << pair{0x4a0, (int16_t)0}; // Disable 630
        };

        constexpr unsigned int st7BossCreateCall = 0x7b38;
        constexpr unsigned int st7bsPrePushSpellID = 0x790;
        constexpr unsigned int st7bsPostNotSpellPracCheck = 0x830;
        constexpr unsigned int st7bsSpellHealthVal = 0x830 + 0x10;
        constexpr unsigned int st7bsSpellSubCallOrd = 0x844 + 0x1c;

        switch (section) {
        case THPrac::TH14::TH14_ST1_MID1:
            ECLJump(ecl, 0x689c, 0x6b08, 60);
            break;
        case THPrac::TH14::TH14_ST1_MID2:
            ECLJump(ecl, 0x689c, 0x6b08, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x478, 0x560, 0);
            ecl << pair{0x38c, (int16_t)0};
            break;
        case THPrac::TH14::TH14_ST1_BOSS1:
            if (thPracParam.dlg)
                ECLJump(ecl, 0x689c, 0x6b94, 60);
            else
                ECLJump(ecl, 0x689c, 0x6bbc, 60);
            break;
        case THPrac::TH14::TH14_ST1_BOSS2:
            ECLJump(ecl, 0x689c, 0x6bbc, 60);
            ecl.SetFile(2);
            ECLJump(ecl, 0x400, 0x4e8, 0); // Utilize Spell Practice Jump
            ecl << pair{0x4f8, 1500}; // Set Health
            ecl << pair{0x518, (int8_t)0x31}; // Set Spell Ordinal
            ecl << pair{0x1aa4, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST1_BOSS3:
            ECLJump(ecl, 0x689c, 0x6bbc, 60);
            ecl.SetFile(2);
            ecl << pair{0x638, (int8_t)0x32}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x1028, (int16_t)0 } << pair { 0x1158, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x12e4, 0} << pair{0x12d0, 0}; // Change Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST1_BOSS4:
            ECLJump(ecl, 0x689c, 0x6bbc, 60);
            ecl.SetFile(2);
            ECLJump(ecl, 0x400, 0x4e8, 0); // Utilize Spell Practice Jump
            ecl << pair{0x4f8, 1900}; // Set Health
            ecl << pair{0x518, (int8_t)0x32}; // Set Spell Ordinal
            ecl << pair{0x2cbc, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST2_MID1:
            ECLJump(ecl, 0x4f30, 0x51e4, 60);
            break;
        case THPrac::TH14::TH14_ST2_MID2:
            ECLJump(ecl, 0x4f30, 0x51e4, 60);
            ecl.SetFile(2);
            ECLJump(ecl, 0x3fc, 0x4e4, 0);
            ecl << pair{0xdf8, (int16_t)0};
            break;
        case THPrac::TH14::TH14_ST2_BOSS1:
            if (thPracParam.dlg)
                ECLJump(ecl, 0x4f30, 0x524c, 60);
            else
                ECLJump(ecl, 0x4f30, 0x5274, 60);
            break;
        case THPrac::TH14::TH14_ST2_BOSS2:
            ECLJump(ecl, 0x4f30, 0x5274, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x4e4, 0x5cc, 0); // Utilize Spell Practice Jump
            ecl << pair{0x5dc, 1400}; // Set Health
            ecl << pair{0x5fc, (int8_t)0x31}; // Set Spell Ordinal
            ecl << pair{0x2678, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST2_BOSS3:
            ECLJump(ecl, 0x4f30, 0x5274, 60);
            ecl.SetFile(3);
            ecl << pair{0x800, (int8_t)0x32}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x13d4, (int16_t)0 } << pair { 0x1518, (int16_t)0 }; // Disable Item Drops & SE
            break;
        case THPrac::TH14::TH14_ST2_BOSS4:
            ECLJump(ecl, 0x4f30, 0x5274, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x4e4, 0x5cc, 0); // Utilize Spell Practice Jump
            ecl << pair{0x5dc, 1900}; // Set Health
            ecl << pair{0x5fc, (int8_t)0x32}; // Set Spell Ordinal
            ecl << pair{0x3410, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST2_BOSS5:
            ECLJump(ecl, 0x4f30, 0x5274, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x4e4, 0x5cc, 0); // Utilize Spell Practice Jump
            ecl << pair{0x5dc, 2100}; // Set Health
            ecl << pair{0x5fc, (int8_t)0x33}; // Set Spell Ordinal
            break;
        case THPrac::TH14::TH14_ST3_MID1:
            ECLJump(ecl, 0x648c, 0x6740, 60);
            break;
        case THPrac::TH14::TH14_ST3_MID2_EN:
            ECLJump(ecl, 0x648c, 0x6740, 60);
            ecl.SetFile(2);
            ECLJump(ecl, 0x520, 0x614, 0);
            ECLJump(ecl, 0x664, 0xc88, 0);
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0xcc8, (int16_t)0 } << pair { 0xe24, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0xf68, 0} << pair{0xf54, 0}; // Change Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST3_MID2_HL:
            ECLJump(ecl, 0x648c, 0x6740, 60);
            ecl.SetFile(2);
            switch (thPracParam.phase) {
            default:
            case 0:
                break;
            case 1:
                ecl << pair { 0x1718,(int16_t)542};
                break;
            }
            ECLJump(ecl, 0x404, 0x4ec, 0);
            break;
        case THPrac::TH14::TH14_ST3_BOSS1:
            if (thPracParam.dlg)
                ECLJump(ecl, 0x648c, 0x67a8, 60);
            else
                ECLJump(ecl, 0x648c, 0x67d0, 60);
            break;
        case THPrac::TH14::TH14_ST3_BOSS2:
            ECLJump(ecl, 0x648c, 0x67d0, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x458, 0x540, 0); // Utilize Spell Practice Jump
            ecl << pair{0x550, 1600}; // Set Health
            ecl << pair{0x570, (int8_t)0x31}; // Set Spell Ordinal
            ecl << pair{0x20f0, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST3_BOSS3:
            ECLJump(ecl, 0x648c, 0x67d0, 60);
            ecl.SetFile(3);
            ecl << pair{0x774, (int8_t)0x32}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0xe7c, (int16_t)0 } << pair { 0xfc0, (int16_t)0 } << pair { 0x1134, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x10b0, 59} << pair{0x1154, 20} << pair{0x112c, 0}; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST3_BOSS4:
            ECLJump(ecl, 0x648c, 0x67d0, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x458, 0x540, 0); // Utilize Spell Practice Jump
            ecl << pair{0x550, 2100}; // Set Health
            ecl << pair{0x570, (int8_t)0x32}; // Set Spell Ordinal
            ecl << pair{0x38c4, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST3_BOSS5:
            ECLJump(ecl, 0x648c, 0x67d0, 60);
            ecl.SetFile(3);
            ecl << pair{0x774, (int8_t)0x33}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x1790, (int16_t)0 } << pair { 0x18d4, (int16_t)0 } << pair { 0x1a48, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x19c4, 59} << pair{0x1a68, 20} << pair{0x1a40, 0}; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST3_BOSS6:
            ECLJump(ecl, 0x648c, 0x67d0, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x458, 0x540, 0); // Utilize Spell Practice Jump
            ecl << pair{0x550, 1800}; // Set Health
            ecl << pair{0x570, (int8_t)0x33}; // Set Spell Ordinal
            ecl << pair{0x47e4, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST4_MID1:
            ECLJump(ecl, 0x97f8, 0x9ae4, 60);
            break;
        case THPrac::TH14::TH14_ST4_MID2:
            ECLJump(ecl, 0x97f8, 0x9ae4, 60);

            // BossA
            ecl.SetFile(2);
            ecl << pair{0x439, (int8_t)0x32};
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x143c, (int16_t)0 } << pair { 0x1598, (int16_t)0 };
            ecl << pair{0x16c8, 0} << pair{0x16dc, (int16_t)0};

            // BossB
            ecl.SetFile(3);
            ecl << pair{0x542, (int8_t)0x32};
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0xca8, (int16_t)0 } << pair { 0xe04, (int16_t)0 };
            ecl << pair{0xf34, 0} << pair{0xf48, (int16_t)0};
            break;
        case THPrac::TH14::TH14_ST4_BOSS1:
            if (thPracParam.dlg)
                ECLJump(ecl, 0x97f8, 0x9b4c, 60);
            else {
                ECLJump(ecl, 0x97f8, 0x9b74, 60);

                // BossA
                ecl.SetFile(4);
                ECLJump(ecl, 0x9950, 0x9ab4, 0); // Skip Dummy Boss
                ecl << pair{0x3e8, 0}; // Cancel Msg Invi.

                // BossB
                ecl.SetFile(5);
                ECLJump(ecl, 0x4dcc, 0x4f30, 0); // Skip Dummy Boss
                ecl << pair{0x358, 0}; // Cancel Msg Invi.
            }
            break;
        case THPrac::TH14::TH14_ST4_BOSS2:
            ECLJump(ecl, 0x97f8, 0x9b74, 60);

            // BossA
            ecl.SetFile(4);
            ECLJump(ecl, 0x9950, 0x9ab4, 0); // Skip Dummy Boss
            ECLJump(ecl, 0x4e8, 0x5d0, 0); // Utilize Spell Practice Jump
            ecl << pair{0x5e0, 2300}; // Set Health
            ecl << pair{0x600, (int8_t)0x31}; // Set Spell Ordinal
            ecl << pair{0x6370, (int16_t)0}; // Disable Item Drops

            // BossB
            ecl.SetFile(5);
            ECLJump(ecl, 0x4dcc, 0x4f30, 0); // Skip Dummy Boss
            ECLJump(ecl, 0x458, 0x540, 0); // Utilize Spell Practice Jump
            ecl << pair{0x550, 2300}; // Set Health
            ecl << pair{0x571, (int8_t)0x31}; // Set Spell Ordinal
            ecl << pair{0x1e98, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST4_BOSS3:
            ECLJump(ecl, 0x97f8, 0x9b74, 60);

            // BossA
            ecl.SetFile(4);
            ECLJump(ecl, 0x9950, 0x9ab4, 0); // Skip Dummy Boss
            ecl << pair{0x3e8, 0}; // Cancel Msg Invi.
            ecl << pair{0x804, (int8_t)0x32}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x24e4, (int16_t)0 } << pair { 0x2628, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x2718, 59} << pair{0x2794, 0}; // Change Move Time & Wait Time

            // BossB
            ecl.SetFile(5);
            ECLJump(ecl, 0x4dcc, 0x4f30, 0); // Skip Dummy Boss
            ecl << pair{0x358, 0}; // Cancel Msg Invi.
            ecl << pair{0x775, (int8_t)0x32}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0xf0c, (int16_t)0 } << pair { 0x1050, (int16_t)0 } << pair { 0x11e4, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x1140, 59} << pair{0x1204, 0} << pair{0x11dc, 0}; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST4_BOSS4:
            ECLJump(ecl, 0x97f8, 0x9b74, 60);

            // BossA
            ecl.SetFile(4);
            ECLJump(ecl, 0x9950, 0x9ab4, 0); // Skip Dummy Boss
            ECLJump(ecl, 0x4e8, 0x5d0, 0); // Utilize Spell Practice Jump
            ecl << pair{0x5e0, 2300}; // Set Health
            ecl << pair{0x600, (int8_t)0x32}; // Set Spell Ordinal
            ecl << pair{0x6c98, (int16_t)0}; // Disable Item Drops

            // BossB
            ecl.SetFile(5);
            ECLJump(ecl, 0x4dcc, 0x4f30, 0); // Skip Dummy Boss
            ECLJump(ecl, 0x458, 0x540, 0); // Utilize Spell Practice Jump
            ecl << pair{0x550, 2300}; // Set Health
            ecl << pair{0x571, (int8_t)0x32}; // Set Spell Ordinal
            ecl << pair{0x2ddc, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST4_BOSS5:
            ECLJump(ecl, 0x97f8, 0x9b74, 60);

            // BossA
            ecl.SetFile(4);
            ECLJump(ecl, 0x9950, 0x9ab4, 0); // Skip Dummy Boss
            ecl << pair{0x3e8, 0}; // Cancel Msg Invi.
            ecl << pair{0x804, (int8_t)0x33}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x44dc, (int16_t)0 } << pair { 0x4634, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x4724, 59} << pair{0x478c, 0}; // Change Move Time & Wait Time

            // BossB
            ecl.SetFile(5);
            ECLJump(ecl, 0x4dcc, 0x4f30, 0); // Skip Dummy Boss
            ecl << pair{0x358, 0}; // Cancel Msg Invi.
            ecl << pair{0x775, (int8_t)0x33}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x1678, (int16_t)0 } << pair { 0x17dc, (int16_t)0 } << pair { 0x1950, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x18cc, 59} << pair{0x1948, 0} << pair{0x1970, 0}; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST4_BOSS6:
            ECLJump(ecl, 0x97f8, 0x9b74, 60);

            // BossA
            ecl.SetFile(4);
            ECLJump(ecl, 0x9950, 0x9ab4, 0); // Skip Dummy Boss
            ECLJump(ecl, 0x4e8, 0x5d0, 0); // Utilize Spell Practice Jump
            ecl << pair{0x5e0, 2800}; // Set Health
            ecl << pair{0x600, (int8_t)0x33}; // Set Spell Ordinal
            ecl << pair{0x7db4, (int16_t)0}; // Disable Item Drops

            // BossB
            ecl.SetFile(5);
            ECLJump(ecl, 0x4dcc, 0x4f30, 0); // Skip Dummy Boss
            ECLJump(ecl, 0x458, 0x540, 0); // Utilize Spell Practice Jump
            ecl << pair{0x550, 2500}; // Set Health
            ecl << pair{0x571, (int8_t)0x33}; // Set Spell Ordinal
            ecl << pair{0x3598, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST5_MID1:
            ECLJump(ecl, 0x8654, 0x8940, 60);
            break;
        case THPrac::TH14::TH14_ST5_MID2:
            ECLJump(ecl, 0x8654, 0x8940, 60);
            ecl.SetFile(2);
            ECLJump(ecl, 0x3c4, 0x4ac, 0);
            ecl << pair{0xd38, (int16_t)0};
            break;
        case THPrac::TH14::TH14_ST5_BOSS1:
            if (thPracParam.dlg)
                ECLJump(ecl, 0x8654, 0x89bc, 60);
            else
                ECLJump(ecl, 0x8654, 0x89e4, 60);
            break;
        case THPrac::TH14::TH14_ST5_BOSS2:
            ECLJump(ecl, 0x8654, 0x89e4, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x43c, 0x524, 0); // Utilize Spell Practice Jump
            ecl << pair{0x534, 2600}; // Set Health
            ecl << pair{0x554, (int8_t)0x31}; // Set Spell Ordinal
            ecl << pair{0x334c, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST5_BOSS3:
            ECLJump(ecl, 0x8654, 0x89e4, 60);
            ecl.SetFile(3);
            ecl << pair{0x83c, (int8_t)0x32}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x14b8, (int16_t)0 } << pair { 0x1638, (int16_t)0 } << pair { 0x17ac, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x1728, 59} << pair{0x17cc, 20} << pair{0x17a4, 0}; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST5_BOSS4:
            ECLJump(ecl, 0x8654, 0x89e4, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x43c, 0x524, 0); // Utilize Spell Practice Jump
            ecl << pair{0x534, 2100}; // Set Health
            ecl << pair{0x554, (int8_t)0x32}; // Set Spell Ordinal
            ecl << pair{0x4350, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST5_BOSS5:
            ECLJump(ecl, 0x8654, 0x89e4, 60);
            ecl.SetFile(3);
            ecl << pair{0x83c, (int8_t)0x33}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x239c, (int16_t)0 } << pair { 0x2530, (int16_t)0 } << pair { 0x26c0, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x2620, 59} << pair{0x26e0, 20} << pair{0x26b8, 0}; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST5_BOSS6:
            ECLJump(ecl, 0x8654, 0x89e4, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x43c, 0x524, 0); // Utilize Spell Practice Jump
            ecl << pair{0x534, 5700}; // Set Health
            ecl << pair{0x554, (int8_t)0x33}; // Set Spell Ordinal
            ecl << pair{0x59ec, (int16_t)0}; // Disable Item Drops

            ECLJump(ecl, 0x538, 0x265c, 0);
            ECLJump(ecl, 0x2694, 0x59d4, 0);
            break;
        case THPrac::TH14::TH14_ST5_BOSS7:
            ECLJump(ecl, 0x8654, 0x89e4, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x43c, 0x524, 0); // Utilize Spell Practice Jump
            ecl << pair{0x534, 2700}; // Set Health
            ecl << pair{0x554, (int8_t)0x34}; // Set Spell Ordinal

            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x604c, (int16_t)0 } << pair { 0x5f3c, (int16_t)0 } << pair { 0x6168, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x6094, 59} << pair{0x60c8, 0} << pair{0x60b4, 0}; // Change Move Time, Wait Time & Inv. Time
            ecl << pair{0x3b0, (int16_t)0}; // Disable 504
            break;
        case THPrac::TH14::TH14_ST6_MID1:
            ECLJump(ecl, 0x79d0, 0x7bd8, 60);
            break;
        case THPrac::TH14::TH14_ST6_MID2:
            ECLJump(ecl, 0x79d0, 0x7bd8, 60);
            ecl.SetFile(2);
            ecl << pair{0x3d5, (int8_t)0x32}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0xda0, (int16_t)0 } << pair { 0xee4, (int16_t)0 } << pair { 0x103c, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0xfd4, 59} << pair{0x105c, 20} << pair{0x1034, 0}; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST6_BOSS1:
            if (thPracParam.dlg)
                ECLJump(ecl, 0x7988, 0x7c40, 60);
            else
                st6_boss(2);
            break;
        case THPrac::TH14::TH14_ST6_BOSS2:
            st6_boss(2);
            ECLJump(ecl, 0x540, 0x628, 0); // Utilize Spell Practice Jump
            ecl << pair{0x638, 3000}; // Set Health
            ecl << pair{0x658, (int8_t)0x31}; // Set Spell Ordinal
            ecl << pair{0x4ac0, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST6_BOSS3:
            st6_boss(3);
            ecl << pair{0xb3c, (int8_t)0x32}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x166c, (int16_t)0 } << pair { 0x17e0, (int16_t)0 } << pair { 0x1940, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x18d0, 59} << pair{0x1960, 0} << pair{0x1938, 60}; // Change Move Time, Wait Time & Inv. Time
            ecl << pair{0x16c4, (int16_t)0}; // Disable 630
            break;
        case THPrac::TH14::TH14_ST6_BOSS4:
            st6_boss(3);
            ECLJump(ecl, 0x540, 0x628, 0); // Utilize Spell Practice Jump
            ecl << pair{0x638, 2800}; // Set Health
            ecl << pair{0x658, (int8_t)0x32}; // Set Spell Ordinal
            ecl << pair{0x58bc, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST6_BOSS5:
            st6_boss(4);
            ecl << pair{0xb3c, (int8_t)0x33}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x27cc, (int16_t)0 } << pair { 0x2924, (int16_t)0 } << pair { 0x2a98, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x2a14, 59} << pair{0x2ab8, 0} << pair{0x2a90, 60}; // Change Move Time, Wait Time & Inv. Time
            ecl << pair{0x2808, (int16_t)0}; // Disable 630
            break;
        case THPrac::TH14::TH14_ST6_BOSS6:
            st6_boss(4);
            ECLJump(ecl, 0x540, 0x7f0, 0); // Utilize Spell Practice Jump
            ecl << pair{0x6928, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST6_BOSS7:
            st6_boss(4);
            ecl << pair{0xb3c, (int8_t)0x34}; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x37f8, (int16_t)0 } << pair { 0x3970, (int16_t)0 } << pair { 0x3ae4, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair{0x3a60, 59} << pair{0x3b04, 0} << pair{0x3adc, 60}; // Change Move Time, Wait Time & Inv. Time
            ecl << pair{0x3834, (int16_t)0}; // Disable 630
            ecl << pair{0x385c, (int16_t)0}; // Disable BossCupEnd
            break;
        case THPrac::TH14::TH14_ST6_BOSS8:
            st6_boss(4);
            ECLJump(ecl, 0x540, 0x628, 0); // Utilize Spell Practice Jump
            ecl << pair{0x638, 1}; // Set Health
            ecl << pair{0x658, (int8_t)0x34}; // Set Spell Ordinal
            ecl << pair{0x75e0, (int16_t)0}; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST6_BOSS9:
            st6_boss(4);

            if (thPracParam.keepSpellDrops) {
                ecl << pair { 0xb3c, (int8_t)0x35 }; // Change Nonspell

            } else {
                ECLJump(ecl, 0x540, 0x628, 0); // Utilize Spell Practice Jump
                ecl << pair { 0x638, 3000 }; // Set Health
                ecl << pair { 0x658, (int8_t)0x35 }; // Set Spell Ordinal
            }
            if (thPracParam.phase == 1) {
                ecl.SetPos(0x8cdc);
                ecl << 30 << 30 << 30 << 30;
            }
            break;
        case THPrac::TH14::TH14_ST6_BOSS10:
            st6_boss(4);
            if (thPracParam.keepSpellDrops) {
                ecl << pair { 0xb3c, (int8_t)0x36 }; // Change Nonspell

            } else {
                ECLJump(ecl, 0x540, 0x628, 0); // Utilize Spell Practice Jump
                ecl << pair { 0x638, 8000 }; // Set Health
                ecl << pair { 0x658, (int8_t)0x36 }; // Set Spell Ordinal
                ECLJump(ecl, 0x63c, 0x4790, 0);
                ECLJump(ecl, 0x47c8, 0x9c18, 0);
            }
            switch (thPracParam.phase) {
            case 1:
                ecl << pair{0x638, 6500};
                ecl << pair{0x9fb0, 0};
                ECLJump(ecl, 0xa188, 0xa2cc, 120);
                break;
            case 2:
                ecl << pair{0x638, 3300};
                ecl << pair{0x9fb0, 0};
                ECLJump(ecl, 0xa188, 0xa2cc, 120);
                ECLJump(ecl, 0xa4a8, 0xa5ec, 120);
                break;
            case 3:
                ecl << pair{0x638, 3300};
                ecl << pair{0x9fb0, 0};
                ECLJump(ecl, 0xa188, 0xa2cc, 120);
                ECLJump(ecl, 0xa4a8, 0xa5ec, 120);
                ECLJump(ecl, 0xa7b4, 0xa828, 120);
                break;
            default:
                break;
            }
            break;
        case THPrac::TH14::TH14_ST7_MID1:
            ECLJump(ecl, st7PostMaple, 0x7a64, 60);
            if (!thPracParam.dlg)
                ecl << pair{0x390c, (int16_t)0};
            break;
        case THPrac::TH14::TH14_ST7_MID2:
            ECLJump(ecl, st7PostMaple, 0x7a64, 60);
            ecl << pair{0x390c, (int16_t)0};
            ecl.SetFile(2);

            ECLJump(ecl, 0x528, 0x590, 0); // Disable Effect
            ecl << pair{0x3b4, -32.0f} << pair{0x3b8, 128.0f}; // Change Pos
            ecl << pair{0x801, (int8_t)0x32}; // Change Nonspell
            ecl << pair{0x1efc, (int16_t)0} << pair{0x2028, (int16_t)0}; // Disable SE
            ecl << pair{0x2048, 0} << pair{0x2020, 0}; // Change Wait Time & Inv. Time
            ecl << pair{0x1fe0, (int16_t)0}; // Void 401

            ECLJump(ecl, 0x2d80, 0x2de8, 0); // Disable Effect
            ecl << pair{0x2c0c, 32.0f} << pair{0x2c10, 128.0f}; // Change Pos
            ecl << pair{0x30b5, (int8_t)0x32}; // Change Nonspell
            ecl << pair{0x2260, (int16_t)0}; // Disable SE
            ecl << pair{0x2370, 0} << pair{0x2164, 0}; // Change Wait Time & Inv. Time
            ecl << pair{0x2344, (int16_t)0}; // Void 401
            break;
        case THPrac::TH14::TH14_ST7_MID3:
            ECLJump(ecl, st7PostMaple, 0x7a64, 60);
            ecl << pair{0x390c, (int16_t)0};
            ecl.SetFile(2);

             if (thPracParam.keepSpellDrops) {
                ECLJump(ecl, 0x528, 0x590 - 0x3c, 0); // Disable Effect
                ecl.SetPos(0x590 - 0x3c); // write custom BossItemPhase call for drops
                ecl << 0 << 0x003c000b << 0x04ff0000 << 0
                    << 0x00000010 << 0x73736f42 << 0x6d657449 << 0x73616850
                    << 0x00000065 << 0x00006969 << 0x0000000f << 0x00006969
                    << 0x0000000f << 0x00006669 << 0x00000040;
                ecl << pair { 0x3b4, -32.0f } << pair { 0x3b8, 160.0f }; // Change Pos

            } else {
                ECLJump(ecl, 0x528, 0x590, 0); // Disable Effect
                ecl << pair { 0x3b4, -128.0f } << pair { 0x3b8, 160.0f }; // Change Pos
                ecl << pair { 0x26c0, 0 } << pair { 0x2698, 0 }; // Change Wait Time & Inv. Time
                ecl << pair { 0x2658, (int16_t)0 }; // Void 401
            }
            ecl << pair{0x801, (int8_t)0x33}; // Change Nonspell
            ecl << pair{0x2574, (int16_t)0} << pair{0x26a0, (int16_t)0}; // Disable SE
            if (thPracParam.keepSpellDrops) {
                ECLJump(ecl, 0x2d80, 0x2de8 - 0x3c, 0); // Disable Effect
                ecl.SetPos(0x2de8 - 0x3c); // write custom BossItemPhase call for drops
                ecl << 0 << 0x003c000b << 0x04ff0000 << 0
                    << 0x00000010 << 0x73736f42 << 0x6d657449 << 0x73616850
                    << 0x00000065 << 0x00006969 << 0x0000000f << 0x00006969
                    << 0x0000000f << 0x00006669 << 0x00000040;
                ecl << pair { 0x2c0c, 32.0f } << pair { 0x2c10, 160.0f }; // Change Pos

            } else {
                ECLJump(ecl, 0x2d80, 0x2de8, 0); // Disable Effect
                ecl << pair { 0x2c0c, 128.0f } << pair { 0x2c10, 160.0f }; // Change Pos
                ecl << pair { 0x29e8, 0 } << pair { 0x27dc, 0 }; // Change Wait Time & Inv. Time
                ecl << pair { 0x29bc, (int16_t)0 }; // Void 401
            }
            ecl << pair{0x30b5, (int8_t)0x33}; // Change Nonspell
            ecl << pair{0x28d8, (int16_t)0}; // Disable SE
            break;
        case THPrac::TH14::TH14_ST7_END_NS1:
            if (thPracParam.dlg)
                ECLJump(ecl, st7PostMaple, 0x7b10, 60);
            else
                ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            break;
        case THPrac::TH14::TH14_ST7_END_S1:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ECLJump(ecl, st7bsPrePushSpellID, st7bsPostNotSpellPracCheck, 0); // Utilize Spell Practice Jump
            ecl << pair { st7bsSpellHealthVal, 2100 }; // Set Health
            ecl << pair { st7bsSpellSubCallOrd, (int8_t)0x31 }; // Set Spell Ordinal
            ecl << pair { 0x691c, (int16_t)0 }; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST7_END_NS2:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ecl << pair { 0xf08, (int8_t)0x32 }; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x190c, (int16_t)0 } << pair { 0x1ab0, (int16_t)0 } << pair { 0x1c48, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair { 0x1ba0, 59 } << pair { 0x1c68, 0 } << pair { 0x1c40, 60 }; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST7_END_S2:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ECLJump(ecl, st7bsPrePushSpellID, st7bsPostNotSpellPracCheck, 0); // Utilize Spell Practice Jump
            ecl << pair { st7bsSpellHealthVal, 3000 }; // Set Health
            ecl << pair { st7bsSpellSubCallOrd, (int8_t)0x32 }; // Set Spell Ordinal
            ecl << pair { 0x9520, (int16_t)0 }; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST7_END_NS3:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ecl << pair { 0xf08, (int8_t)0x33 }; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x22b4, (int16_t)0 } << pair { 0x2460, (int16_t)0 } << pair { 0x25f8, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair { 0x2550, 59 } << pair { 0x2618, 0 } << pair { 0x25f0, 60 }; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST7_END_S3:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ECLJump(ecl, st7bsPrePushSpellID, st7bsPostNotSpellPracCheck, 0); // Utilize Spell Practice Jump
            ecl << pair { st7bsSpellHealthVal, 2300 }; // Set Health
            ecl << pair { st7bsSpellSubCallOrd, (int8_t)0x33 }; // Set Spell Ordinal
            ecl << pair { 0xa670, (int16_t)0 }; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST7_END_NS4:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ecl << pair { 0xf08, (int8_t)0x34 }; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x2c00, (int16_t)0 } << pair { 0x2da4, (int16_t)0 } << pair { 0x2f3c, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair { 0x2e94, 59 } << pair { 0x2f5c, 0 } << pair { 0x2f34, 60 }; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST7_END_S4:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ECLJump(ecl, st7bsPrePushSpellID, st7bsPostNotSpellPracCheck, 0); // Utilize Spell Practice Jump
            ecl << pair { st7bsSpellHealthVal, 3500 }; // Set Health
            ecl << pair { st7bsSpellSubCallOrd, (int8_t)0x34 }; // Set Spell Ordinal
            ecl << pair { 0xae74, (int16_t)0 }; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST7_END_NS5:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ecl << pair { 0xf08, (int8_t)0x35 }; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x34ec, (int16_t)0 } << pair { 0x3690, (int16_t)0 } << pair { 0x3828, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair { 0x3780, 59 } << pair { 0x3848, 0 } << pair { 0x3820, 60 }; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST7_END_S5:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ECLJump(ecl, st7bsPrePushSpellID, st7bsPostNotSpellPracCheck, 0); // Utilize Spell Practice Jump
            ecl << pair { st7bsSpellHealthVal, 3300 }; // Set Health
            ecl << pair { st7bsSpellSubCallOrd, (int8_t)0x35 }; // Set Spell Ordinal
            ecl << pair { 0xb558, (int16_t)0 }; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST7_END_NS6:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ecl << pair { 0xf08, (int8_t)0x36 }; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x4300, (int16_t)0 } << pair { 0x44ac, (int16_t)0 } << pair { 0x4644, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair { 0x459c, 59 } << pair { 0x4664, 0 } << pair { 0x463c, 60 }; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST7_END_S6:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ECLJump(ecl, st7bsPrePushSpellID, st7bsPostNotSpellPracCheck, 0); // Utilize Spell Practice Jump
            ecl << pair { st7bsSpellHealthVal, 3000 }; // Set Health
            ecl << pair { st7bsSpellSubCallOrd, (int8_t)0x36 }; // Set Spell Ordinal
            ecl << pair { 0xc5f0, (int16_t)0 }; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST7_END_NS7:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ecl << pair { 0xf08, (int8_t)0x37 }; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x4c3c, (int16_t)0 } << pair { 0x4de0, (int16_t)0 } << pair { 0x4f78, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair { 0x4ed0, 59 } << pair { 0x4f98, 0 } << pair { 0x4f70, 60 }; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST7_END_S7:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ECLJump(ecl, st7bsPrePushSpellID, st7bsPostNotSpellPracCheck, 0); // Utilize Spell Practice Jump
            ecl << pair { st7bsSpellHealthVal, 3500 }; // Set Health
            ecl << pair { st7bsSpellSubCallOrd, (int8_t)0x37 }; // Set Spell Ordinal
            ecl << pair { 0xd538, (int16_t)0 }; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST7_END_NS8:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ecl << pair { 0xf08, (int8_t)0x38 }; // Change Nonspell
            if (!thPracParam.keepSpellDrops)
                ecl << pair { 0x5938, (int16_t)0 } << pair { 0x5ae4, (int16_t)0 } << pair { 0x5c7c, (int16_t)0 }; // Disable Item Drops & SE
            ecl << pair { 0x5bd4, 59 } << pair { 0x5c9c, 0 } << pair { 0x5c74, 60 }; // Change Move Time, Wait Time & Inv. Time
            break;
        case THPrac::TH14::TH14_ST7_END_S8:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ECLJump(ecl, st7bsPrePushSpellID, st7bsPostNotSpellPracCheck, 0); // Utilize Spell Practice Jump
            ecl << pair { st7bsSpellHealthVal, 3500 }; // Set Health
            ecl << pair { st7bsSpellSubCallOrd, (int8_t)0x38 }; // Set Spell Ordinal
            ecl << pair { 0xe638, (int16_t)0 }; // Disable Item Drops
            break;
        case THPrac::TH14::TH14_ST7_END_S9:
        {
            constexpr unsigned int st7BossSpell9Duration = 0xf4f8 + 0x10;
            constexpr unsigned int st7BossSpell9BLSAtk1 = 0xf924;
            constexpr unsigned int st7BossSpell9BLSAtk2 = 0xf984;
            constexpr unsigned int st7BossSpell9BLSAtk3 = 0xf9e4;
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            if (thPracParam.keepSpellDrops) {
                ecl << pair { 0xf08, (int8_t)0x39 }; // Change Nonspell

            } else {
                ECLJump(ecl, st7bsPrePushSpellID, st7bsPostNotSpellPracCheck, 0); // Utilize Spell Practice Jump
                ecl << pair { st7bsSpellHealthVal, 4000 }; // Set Health
                ecl << pair { st7bsSpellSubCallOrd, (int8_t)0x39 }; // Set Spell ID in sub call to 9
            }

            switch (thPracParam.phase) {
            case 1:
                ecl << pair { st7BossSpell9Duration, 75 - 22 }; // Adjust duration (skipping 1200f atk 1 + 120f wait)
                ECLJump(ecl, st7BossSpell9BLSAtk1, st7BossSpell9BLSAtk2, 120);
                break;
            case 2:
                ecl << pair { st7BossSpell9Duration, 75 - 44 }; // Adjust duration (idem + 1200f atk2 + 120f wait)
                ECLJump(ecl, st7BossSpell9BLSAtk1, st7BossSpell9BLSAtk3, 120 * 2);
                break;
            default:
                break;
            }
        }
            
            break;
        case THPrac::TH14::TH14_ST7_END_S10:
            ECLJump(ecl, st7PostMaple, st7BossCreateCall, 60);
            ecl.SetFile(3);
            ECLJump(ecl, 0x790, st7bsPostNotSpellPracCheck, 0); // Utilize Spell Practice Jump
            ecl << pair{st7bsSpellHealthVal, 8000}; // Set Health
            ecl << pair{st7bsSpellHealthVal, (int8_t)0x31} << pair{0x861, (int8_t)0x30}; // Set Spell Ordinal
            ecl << pair{0x6d64, (int16_t)0}; // Disable Item Drops

            switch (thPracParam.phase) {
            case 1:
                ecl << pair{0x840, 6200};
                ECLJump(ecl, 0x70e4, 0x7344, 60);
                break;
            case 2:
                ecl << pair{0x840, 4400};
                ECLJump(ecl, 0x70e4, 0x75b8, 60);
                break;
            case 3:
                ecl << pair{0x840, 2600};
                ECLJump(ecl, 0x70e4, 0x78a0, 60);
                break;
            case 4:
                ecl << pair{0x840, 800};
                ECLJump(ecl, 0x70e4, 0x78a0, 60);
                ECLJump(ecl, 0x78f0, 0x7ba0, 60);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
    __declspec(noinline) void THPatchSP(PracticeECL& ecl)
    {
        uint32_t spell_id = *(int32_t*)(0x4f5844);
        switch (spell_id) {
        case 100:
        case 101:
        case 102:
        case 103:
            ecl.SetFile(3);
            if (thPracParam.phase == 1) {
                ecl.SetPos(0x8cdc);
                ecl << 30 << 30 << 30 << 30;
            }
            break;
        case 104:
        case 105:
        case 106:
        case 107:
            ecl.SetFile(3);
            switch (thPracParam.phase) {
            case 1:
                ecl << pair{0xae0, 6500};
                ecl << pair{0x9fb0, 0};
                ECLJump(ecl, 0xa188, 0xa2cc, 120);
                break;
            case 2:
                ecl << pair{0xae0, 3300};
                ecl << pair{0x9fb0, 0};
                ECLJump(ecl, 0xa188, 0xa2cc, 120);
                ECLJump(ecl, 0xa4a8, 0xa5ec, 120);
                break;
            case 3:
                ecl << pair{0xae0, 3300};
                ecl << pair{0x9fb0, 0};
                ECLJump(ecl, 0xa188, 0xa2cc, 120);
                ECLJump(ecl, 0xa4a8, 0xa5ec, 120);
                ECLJump(ecl, 0xa7b4, 0xa828, 120);
                break;
            default:
                break;
            }
            break;
        case 119:
            ecl.SetFile(3);
            switch (thPracParam.phase) {
            case 1:
                ecl << pair{0xe94, 6200};
                ECLJump(ecl, 0x70e4, 0x7344, 60);
                break;
            case 2:
                ecl << pair{0xe94, 4400};
                ECLJump(ecl, 0x70e4, 0x75b8, 60);
                break;
            case 3:
                ecl << pair{0xe94, 2600};
                ECLJump(ecl, 0x70e4, 0x78a0, 60);
                break;
            case 4:
                ecl << pair{0xe94, 800};
                ECLJump(ecl, 0x70e4, 0x78a0, 60);
                ECLJump(ecl, 0x78f0, 0x7ba0, 60);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
    __declspec(noinline) void THSectionPatch(PracticeEdits* edits)
    {
        PracticeECL ecl((void*)GetMemAddr(0x4db544, 0xcc, 0xC), edits);

        auto section = thPracParam.section;
        if (section >= 10000 && section < 20000) {
            int stage = (section - 10000) / 100;
            int portionId = (section - 10000) % 100;
            THStageWarp(ecl, stage, portionId);
        } else {
            THPatch(ecl, (th_sections_t)section);
        }
    }

    namespace Pointdevice {
        enum class Section {
            Opening, Front2, Front3, Front4,
            Mid1, Mid2, MidWave, Latter1, Latter2,
            Boss1, Card1, Boss2, Card2, Boss3, Card3, Boss4, Card4, Card5, Card6,
        };
        using enum Section;
        enum class Retry { Playing, Covering, Requested, Restoring, Ready, Go };
        enum class BossRetryMusic { None, Start, Keep };

        static constexpr ULONGLONG coverInMs = 176;
        static constexpr ULONGLONG restartPulseMs = 200; // 5 Hz
        static constexpr ULONGLONG readyPulseMs = 188;
        static constexpr ULONGLONG readyDurationMs = 3 * readyPulseMs;
        static constexpr ULONGLONG goDurationMs = 340;
        static constexpr ULONGLONG chapterFinishFadeAtMs = 1280;
        static constexpr ULONGLONG chapterFinishDurationMs = 1500;

        struct Instruction {
            unsigned file;
            unsigned offset;
            uint16_t opcode;
        };

        struct Jump {
            Instruction from {};
            unsigned destination = 0;
            float time = 0;
        };

        struct Checkpoint {
            unsigned file;
            unsigned offset; // first instruction of the subroutine (opcode 40)
            Section section;
            Jump phaseJump {};
            int health = 0;
            int scene = 0;
        };

        struct Stage {
            unsigned front;
            unsigned midboss;
            unsigned latter;
            unsigned boss;
            std::span<const Checkpoint> checkpoints;
            std::span<const Instruction> midbossExits {};
            unsigned titleWait; // LogoEnemy's ins_23, followed by ins_554 and ins_10
            std::array<Instruction, 2> bossEntranceWaits;
        };

        // Japanese 1.00b ECL. File 0 is the stage, file 1 is default.ecl,
        // and the boss files follow in the order listed by the stage's ECLI.
        static constexpr Checkpoint stage1[] {
            { 0, 0x441c, Opening }, // MainSub00
            { 0, 0x47d8, Front2, { { 0, 0x40e4, 11 }, 0x4118 } },
            { 0, 0x495c, Front3, { { 0, 0x40e4, 11 }, 0x414c } },
            { 3, 0x00c8, Mid1 }, // MBoss
            { 3, 0x12a4, Mid2, { { 3, 0x0478, 42 }, 0x0560 } }, // MBossCard1
            { 0, 0x5104, Latter1 }, // MainSub04
            { 0, 0x548c, Latter2, { { 0, 0x41e8, 11 }, 0x4218 } },
            { 2, 0x065c, Boss1 },
            { 2, 0x1a8c, Card1, { { 2, 0x0400, 42 }, 0x04e8 } },
            { 2, 0x0fdc, Boss2, { { 2, 0x065c, 40 }, 0x0fdc } },
            { 2, 0x2ca4, Card2, { { 2, 0x0400, 42 }, 0x05cc } },
        };
        static constexpr Checkpoint stage2[] {
            { 0, 0x3400, Opening },
            { 0, 0x3534, Front2, { { 0, 0x3004, 11 }, 0x3038 } },
            { 0, 0x38b8, Front3, { { 0, 0x3004, 11 }, 0x306c } },
            { 2, 0x012c, Mid1 },
            { 2, 0x0de0, Mid2, { { 2, 0x03fc, 42 }, 0x04e4 } },
            { 0, 0x3da4, MidWave, { { 0, 0x31c8, 524 }, 0x3220 } },
            { 0, 0x4064, Latter1, { { 0, 0x3118, 23 }, 0x312c } },
            { 0, 0x4374, Latter2, { { 0, 0x3118, 23 }, 0x3160 } },
            { 3, 0x0824, Boss1 },
            { 3, 0x2660, Card1, { { 3, 0x04e4, 42 }, 0x05cc } },
            { 3, 0x13bc, Boss2, { { 3, 0x0824, 40 }, 0x13bc } },
            { 3, 0x33f8, Card2, { { 3, 0x04e4, 42 }, 0x06b0 } },
            { 3, 0x2368, Card3, { { 3, 0x0824, 40 }, 0x2368 } }, // Boss3 prepares the card
        };
        static constexpr Checkpoint stage3[] {
            { 0, 0x2610, Opening },
            { 0, 0x2a2c, Front2, { { 0, 0x21f4, 11 }, 0x2228 } },
            { 0, 0x3270, Front3, { { 0, 0x21f4, 11 }, 0x2290 } },
            { 2, 0x00e8, Mid1 },
            { 2, 0x0c88, Mid2, { { 2, 0x05b4, 40 }, 0x0c88 } }, // MBoss2 selects the H/L card
            { 0, 0x37c4, MidWave, { { 0, 0x23c4, 524 }, 0x241c } },
            { 0, 0x3bb8, Latter1 },
            { 0, 0x4064, Latter2, { { 0, 0x22f8, 11 }, 0x233c } },
            { 3, 0x0798, Boss1 },
            { 3, 0x20d8, Card1, { { 3, 0x0458, 42 }, 0x0540 } },
            { 3, 0x0e38, Boss2, { { 3, 0x0798, 40 }, 0x0e38 } },
            { 3, 0x38ac, Card2, { { 3, 0x0458, 42 }, 0x0624 } },
            { 3, 0x174c, Boss3, { { 3, 0x0798, 40 }, 0x174c } },
            { 3, 0x47cc, Card3, { { 3, 0x0458, 42 }, 0x0708 } },
        };
        static constexpr Checkpoint stage4[] {
            { 0, 0x55d4, Opening },
            { 0, 0x60f4, Front2, { { 0, 0x5090, 11 }, 0x50c4 } },
            { 0, 0x6224, Front3, { { 0, 0x5090, 11 }, 0x50f8 } },
            { 0, 0x6924, Front4, { { 0, 0x5090, 11 }, 0x512c } },
            { 2, 0x00ec, Mid1 }, // MBoss
            { 2, 0x1410, Mid2, { { 2, 0x04b4, 40 }, 0x1410 } },
            { 3, 0x00d8, Mid1 }, // MBossB
            { 3, 0x0c7c, Mid2, { { 3, 0x05bc, 40 }, 0x0c7c } },
            { 0, 0x6a54, MidWave, { { 0, 0x5240, 524 }, 0x53c0 } },
            { 0, 0x6d88, Latter1 },
            { 0, 0x6f14, Latter2, { { 0, 0x5194, 11 }, 0x51d8 } },
            { 4, 0x0828, Boss1 },
            { 4, 0x6358, Card1, { { 4, 0x04e8, 42 }, 0x05d0 } },
            { 4, 0x24a0, Boss2, { { 4, 0x0828, 40 }, 0x24a0 } },
            { 4, 0x6c80, Card2, { { 4, 0x04e8, 42 }, 0x06b4 } },
            { 4, 0x4498, Boss3, { { 4, 0x0828, 40 }, 0x4498 } },
            { 4, 0x7d9c, Card3, { { 4, 0x04e8, 42 }, 0x0798 } },
            { 5, 0x0798, Boss1 }, // BossB1
            { 5, 0x1e80, Card1, { { 5, 0x0458, 42 }, 0x0540 } },
            { 5, 0x0ec8, Boss2, { { 5, 0x0798, 40 }, 0x0ec8 } },
            { 5, 0x2dc4, Card2, { { 5, 0x0458, 42 }, 0x0624 } },
            { 5, 0x1634, Boss3, { { 5, 0x0798, 40 }, 0x1634 } },
            { 5, 0x3580, Card3, { { 5, 0x0458, 42 }, 0x0708 } },
        };
        static constexpr Checkpoint stage5[] {
            { 0, 0x2b5c, Opening },
            { 0, 0x2dfc, Front2, { { 0, 0x2784, 11 }, 0x27b8 } },
            { 0, 0x3e90, Front3, { { 0, 0x2784, 11 }, 0x27ec } },
            { 0, 0x44c4, Front4, { { 0, 0x2784, 11 }, 0x2820 } },
            { 2, 0x00a8, Mid1 },
            { 2, 0x0d20, Mid2, { { 2, 0x03c4, 42 }, 0x04ac } },
            { 0, 0x4a7c, Latter1 }, // MainSub04 and MainSub05 run together
            { 0, 0x53e8, Latter2, { { 0, 0x2888, 11 }, 0x28dc } },
            { 3, 0x0860, Boss1 },
            { 3, 0x3334, Card1, { { 3, 0x043c, 42 }, 0x0524 } },
            { 3, 0x1474, Boss2, { { 3, 0x0860, 40 }, 0x1474 } },
            { 3, 0x4338, Card2, { { 3, 0x043c, 42 }, 0x0608 } },
            { 3, 0x2358, Boss3, { { 3, 0x0860, 40 }, 0x2358 } },
            { 3, 0x59d4, Card3, { { 3, 0x043c, 42 }, 0x06ec }, 5700 }, // story HP differs from spell practice
            { 3, 0x5e30, Card4, { { 3, 0x043c, 42 }, 0x07d0 } },
        };
        static constexpr Checkpoint stage6[] {
            { 0, 0x2fac, Opening },
            { 0, 0x327c, Front2, { { 0, 0x2bc0, 11 }, 0x2bf4 } },
            { 0, 0x37d0, Front3, { { 0, 0x2bc0, 11 }, 0x2c28 } },
            { 0, 0x41c0, Front4, { { 0, 0x2bc0, 11 }, 0x2c5c } },
            { 2, 0x00a0, Mid1 },
            { 2, 0x0d5c, Mid2, { { 2, 0x0450, 40 }, 0x0d5c } },
            { 3, 0x0b60, Boss1 },
            { 3, 0x4aa8, Card1, { { 3, 0x0540, 42 }, 0x0628 } },
            { 3, 0x1628, Boss2, { { 3, 0x0b60, 40 }, 0x1628 } },
            { 3, 0x58a4, Card2, { { 3, 0x0540, 42 }, 0x070c }, 0, 3 },
            { 3, 0x2788, Boss3, { { 3, 0x0b60, 40 }, 0x2788 } },
            { 3, 0x6910, Card3, { { 3, 0x0540, 42 }, 0x07f0 }, 0, 4 },
            { 3, 0x37b4, Boss4, { { 3, 0x0b60, 40 }, 0x37b4 } },
            { 3, 0x75c8, Card4, { { 3, 0x0540, 42 }, 0x0908 }, 0, 4 },
            { 3, 0x40dc, Card5, { { 3, 0x0b60, 40 }, 0x40dc } }, // Boss5/6 prepare the last two cards
            { 3, 0x4580, Card6, { { 3, 0x0b60, 40 }, 0x4580 } },
        };

        // The instruction after boss mode is cleared in each midboss exit.
        static constexpr Instruction exits1[] { { 3, 0x2408, 557 }, { 3, 0x2634, 502 } };
        static constexpr Instruction exits2[] { { 2, 0x1cb0, 517 }, { 2, 0x1f34, 502 } };
        static constexpr Instruction exits3[] { { 2, 0x26d0, 517 }, { 2, 0x2954, 502 } };
        static constexpr Instruction exits4[] {
            { 2, 0x2a30, 517 }, { 2, 0x2cb4, 502 }, { 3, 0x2380, 517 }, { 3, 0x2604, 502 },
        };
        static constexpr Instruction exits5[] { { 2, 0x1920, 517 }, { 2, 0x1ba4, 502 } };

        static constexpr Stage stages[] {
            { 0x6ae8, 0x6b08, 0x6b4c, 0x6bbc, stage1, exits1, 0x3e68, { { { 2, 0x0284, 23 } } } },
            { 0x51c4, 0x51e4, 0x5218, 0x5274, stage2, exits2, 0x2d9c, { { { 3, 0x0368, 23 } } } },
            { 0x6720, 0x6740, 0x6774, 0x67d0, stage3, exits3, 0x1f9c, { { { 3, 0x02f8, 23 } } } },
            { 0x9ab4, 0x9ae4, 0x9b18, 0x9b74, stage4, exits4, 0x4b90, { { { 4, 0x0340, 23 }, { 5, 0x02b0, 23 } } } },
            { 0x8910, 0x8940, 0x8988, 0x89e4, stage5, exits5, 0x252c, { { { 3, 0x02dc, 23 } } } },
            { 0x7bb8, 0x7bd8, 0,      0x7c68, stage6, {}, 0x2958, { { { 3, 0x03cc, 23 } } } },
        };

        // Story resources, including an unfinished item collection bonus.
        static constexpr uintptr_t resourceAddresses[] {
            0x4f5830, 0x4f583c, 0x4f5840, 0x4f5848, // score, rank, graze, items
            0x4f584c, 0x4f5858, // point item value, power
            0x4f5864, 0x4f5868, 0x4f586c, 0x4f5870, 0x4f5874, // lives, extends, bombs and fragments
            0x4f5878, 0x4f5880, 0x4f5884, 0x4f5888, 0x4f588c, 0x4f5890, 0x4f5894, // item bonus
        };

        static constexpr size_t itemCount = 0x1258, itemStride = 0xc18;
        static constexpr uintptr_t itemArrayOffset = 0x14;

        struct ItemSnapshot {
            std::array<float, 3> position; // +bac
            std::array<float, 3> velocity; // +bb8
            std::array<uint32_t, 3> timer; // +bc8, before the timer's clock pointer
            int motion; // +bf0
            int type; // +bf4
            float homingSpeed; // +bfc
            int delay; // +c00
        };

        static struct {
            unsigned stage = 0;
            Retry retry = Retry::Playing;
            bool stageLoadComplete = false;
            ULONGLONG retryShownAt = 0;
            ULONGLONG restartShownAt = 0;
            bool retryFrameDrawn = false;
            bool coverFrameDrawn = false;
            bool coverPresented = false;
            bool pausedRetryMusic = false;
            const Checkpoint* checkpoint = nullptr;
            const Checkpoint* practiceEntry = nullptr;
            const Checkpoint* waitingForMidboss = nullptr;
            PracticeEdits practiceEdits;
            ULONGLONG chapterFinishStartedAt = 0;
            bool clearedPreviousStage = false;
            std::array<uintptr_t, 6> files {};
            std::array<Jump, 4> jumps {};
            std::array<uint32_t, elementsof(resourceAddresses)> resources {};
            ChapterCounts chapterCounts {};
            std::vector<ItemSnapshot> items;
            bool hasItemSnapshot = false;
            std::array<float, 3> playerPosition {};
            std::array<int32_t, 2> playerFixedPosition {};
            // Previous, resolved, absolute and relative movement, plus the eight
            // interpolators consumed by the enemy movement update at 0x4230d0.
            std::array<uint8_t, 4 * 0x44> bossMovement {};
            std::array<uint8_t, 2 * 0x68 + 4 * 0x30 + 2 * 0x44> bossInterpolators {};
            std::array<uint8_t, 0xc> bossInvulnerability {}; // Timer values, excluding the clock pointer.
            bool hasPlayerPosition = false;
            bool hasBossPosition = false;
            uint32_t bossItemFlag = 0;
            uint8_t bossTimeout = 0;
            double stageMusicPosition = 0;
            bool deferredStageMusic = false;
            bool seekStageMusic = false;
            BossRetryMusic bossRetryMusic = BossRetryMusic::None;
            bool holdMidbossMusic = false;
            // STD playback state; exclude ANM objects, callbacks and allocations.
            std::array<uint8_t, 0x2c> backgroundTimers {}; // +20..4c
            std::array<uint8_t, 0x1b0> backgroundInterpolators {}; // +50..200
            std::array<uint8_t, 0x12c> backgroundCamera {}; // +4274..43a0
            uintptr_t backgroundInstruction = 0; // relative to the STD script
            uint32_t backgroundDrawDistance = 0;
            bool hasBackground = false;
            uintptr_t titleVm = 0;
            float titleTime = 0;
            bool titlePending = false;
            bool restoreTitle = false;
        } run;

        static constexpr uint16_t markerOpcode = 93;

        static void RestoreStageTitle(const Stage& stage)
        {
            if (!run.titleVm)
                return;
            run.restoreTitle = false;
            auto& time = *reinterpret_cast<float*>(run.titleVm);
            if (run.titlePending) {
                time = run.titleTime;
            } else {
                // Finish the rebuilt LogoEnemy without starting a second title.
                *reinterpret_cast<uintptr_t*>(run.titleVm + 4) = run.files[0] + stage.titleWait + 0x24;
                time = 0;
                run.titleVm = 0;
            }
        }

        static bool IsStoryStage(unsigned stage)
        {
            return pointdeviceRun && thPracParam.mode <= 1 && stage >= 1 && stage <= elementsof(stages);
        }

        static void Reset()
        {
            run = {};
        }

        static void SetRetryPhase(Retry phase)
        {
            run.retry = phase;
            if (phase == Retry::Playing)
                run.holdMidbossMusic = false;
            run.retryShownAt = 0;
            run.retryFrameDrawn = false;
            run.coverFrameDrawn = false;
            run.coverPresented = false;
        }

        static ULONGLONG RetryElapsed(ULONGLONG now)
        {
            return run.retryShownAt ? now - run.retryShownAt : 0;
        }

        static void RetryPresented(ULONGLONG now, bool succeeded)
        {
            if (succeeded && run.retryFrameDrawn) {
                if (!run.retryShownAt)
                    run.retryShownAt = now;
                if (run.retry == Retry::Covering && !run.restartShownAt)
                    run.restartShownAt = now;
                run.coverPresented |= run.coverFrameDrawn;
            }
            run.retryFrameDrawn = false;
            run.coverFrameDrawn = false;
        }

        static bool RetryRebuilding()
        {
            return run.retry == Retry::Requested || run.retry == Retry::Restoring;
        }

        static bool RetryFreezesGameplay()
        {
            return run.retry == Retry::Covering || run.retry == Retry::Ready || run.retry == Retry::Go;
        }

        static constexpr unsigned retryVirtualKeys[] { VK_SHIFT, VK_LSHIFT, VK_RSHIFT, 'Z', 'X', VK_ESCAPE };
        static constexpr unsigned retryScanCodes[] { DIK_LSHIFT, DIK_RSHIFT, DIK_Z, DIK_X, DIK_ESCAPE };

        static bool IgnoreRetryKey(WPARAM key)
        {
            return run.retry != Retry::Playing && std::ranges::find(retryVirtualKeys, key) != std::end(retryVirtualKeys);
        }

        static void FilterRetryKeyboard(PCONTEXT ctx, std::span<const unsigned> blockedKeys)
        {
            if (run.retry == Retry::Playing)
                return;
            auto* keyboard = reinterpret_cast<uint8_t*>(ctx->Ebp - 0x104);
            for (const auto key : blockedKeys)
                keyboard[key] = 0;
        }

        static float EaseRetry(float progress)
        {
            const float t = std::clamp(progress, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        static float RetryPulseOpacity(ULONGLONG elapsed, ULONGLONG period)
        {
            const float pulse = static_cast<float>(elapsed % period) / period;
            return EaseRetry(pulse / 0.20f) * (1.0f - EaseRetry((pulse - 0.55f) / 0.32f));
        }

        static float ChapterFinishOpacity(ULONGLONG elapsed)
        {
            constexpr auto lastPulseAt = 2 * readyPulseMs;
            if (elapsed < lastPulseAt)
                return RetryPulseOpacity(elapsed, readyPulseMs);
            if (elapsed < chapterFinishFadeAtMs)
                return EaseRetry(static_cast<float>(elapsed - lastPulseAt) / (0.20f * readyPulseMs));
            return 1.0f - EaseRetry(static_cast<float>(elapsed - chapterFinishFadeAtMs)
                / (chapterFinishDurationMs - chapterFinishFadeAtMs));
        }

        static float RestartOpacity(ULONGLONG now)
        {
            const auto elapsed = run.restartShownAt ? now - run.restartShownAt : 0;
            // Align the first bright pulse with the end of the cover's sweep.
            return RetryPulseOpacity(elapsed + restartPulseMs / 2, restartPulseMs);
        }

        static void FinishRetryMusic(bool pause)
        {
            const auto musicMode = GetMemContent<uint8_t>(0x4d9151);
            if (run.bossRetryMusic == BossRetryMusic::Keep || !musicMode)
                return;

            // Finish any play/seek command under Restart, including a boss
            // track that had to be loaded because it was no longer playing.
            const auto updateMusic = reinterpret_cast<int(__cdecl*)()>(0x46e5e0);
            while (updateMusic()) {
            }
            if (!pause || musicMode != 1)
                return;

            const auto player = GetMemContent(0x4fd180);
            if (player && GetMemContent<int>(player + 0x50) && !GetMemContent<int>(player + 0x9c)) {
                reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x481300)(player);
                run.pausedRetryMusic = true;
            }
        }

        static void StartDeferredStageMusic()
        {
            if (!run.deferredStageMusic)
                return;
            run.deferredStageMusic = false;
            run.seekStageMusic = run.stageMusicPosition >= 0.001;
            const auto stageInfo = GetMemContent(0x4db69c);
            reinterpret_cast<void(__stdcall*)(int, int)>(0x445710)(0, GetMemContent<int>(stageInfo + 0x30));
        }

        static bool AdvanceRetry(ULONGLONG now)
        {
            const auto elapsed = RetryElapsed(now);
            switch (run.retry) {
            case Retry::Covering:
                if (run.coverPresented) {
                    SetRetryPhase(Retry::Requested);
                    *reinterpret_cast<int32_t*>(SUPERVISOR_NEXT_STATE) = 10; // native restart
                }
                break;
            case Retry::Ready:
                if (elapsed >= readyDurationMs)
                    SetRetryPhase(Retry::Go);
                break;
            case Retry::Go:
                if (elapsed >= goDurationMs) {
                    if (run.holdMidbossMusic) {
                        run.holdMidbossMusic = false;
                        StartDeferredStageMusic();
                        FinishRetryMusic(false);
                    } else if (run.pausedRetryMusic) {
                        const auto player = GetMemContent(0x4fd180);
                        if (player)
                            reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x481390)(player);
                        run.pausedRetryMusic = false;
                    }
                    SetRetryPhase(Retry::Playing);
                }
                break;
            default:
                break;
            }
            return RetryFreezesGameplay();
        }

        static uintptr_t MusicPlayer(unsigned slot)
        {
            // thbgm.dat playback. The other backend uses separate WAV streams.
            if (GetMemContent<uint8_t>(0x4d916c) & 0x10)
                return 0;
            const auto player = GetMemContent(0x4fd180);
            if (!player)
                return 0;
            const auto name = reinterpret_cast<const char*>(0x4f9e00);
            const auto loaded = reinterpret_cast<const char*>(0x4fc080 + slot * 0x100);
            return *name && std::strcmp(name, loaded) == 0 ? player : 0;
        }

        static bool IsMusicPlaying(unsigned slot)
        {
            const auto player = MusicPlayer(slot);
            return player && GetMemContent<int>(player + 0x50);
        }

        static bool IsMidbossCheckpoint(Section section)
        {
            return section == Mid1 || section == Mid2;
        }

        static bool HoldMidbossMusicCommand(int command)
        {
            // Native BGM queue commands for Play, Resume and seeking a new track.
            return run.holdMidbossMusic && (command == 2 || command == 7 || command == 8);
        }

        static bool IsRetryTrackPlaying(int slot)
        {
            if (!(GetMemContent<uint8_t>(0x4d916c) & 0x10))
                return IsMusicPlaying(slot);

            // The separate WAV backend keeps its track name and thread here.
            const auto name = reinterpret_cast<const char*>(0x4f94dc);
            const auto loaded = reinterpret_cast<const char*>(0x4fc080 + slot * 0x100);
            return GetMemContent(0x4f7b6c) && *name && std::strcmp(name, loaded) == 0;
        }

        static void StageMusicAtCheckpoint(bool restoring)
        {
            if (restoring) {
                if (!run.holdMidbossMusic)
                    StartDeferredStageMusic();
            } else {
                // The pause menu uses this same loop-aware position in seconds.
                run.stageMusicPosition = IsMusicPlaying(0) ? reinterpret_cast<double(__vectorcall*)()>(0x44beb0)() : 0;
            }
        }

        static void SeekStageMusic(PCONTEXT ctx)
        {
            // The command's slot has already been replaced by native playback state.
            // Match the prepared track when the game is about to start its buffer.
            if (!run.seekStageMusic)
                return;
            const auto player = MusicPlayer(0);
            if (!player)
                return;
            run.seekStageMusic = false;
            const auto buffers = GetMemContent(player + 4);
            if (!buffers || !GetMemContent(buffers))
                return;

            // Native seek refills and starts the buffer with the stored Play flags.
            const auto args = reinterpret_cast<const int*>(ctx->Esp);
            *reinterpret_cast<int*>(player + 0x20) = args[0];
            *reinterpret_cast<int*>(player + 0x24) = args[1];
            // TH14 expects the position in XMM1. The first double occupies XMM0.
            reinterpret_cast<void(__vectorcall*)(uintptr_t, double, double)>(0x481ee0)(player, 0.0, run.stageMusicPosition);
            ctx->Esp += 3 * sizeof(int);
            ctx->Eip = 0x46e91a;
        }

        static void CheckpointBackground(bool restoring)
        {
            const auto background = GetMemContent(0x4db528);
            const auto script = background ? GetMemContent(background + 0x1c) : 0;
            const auto instruction = background ? GetMemContent(background + 0x4c) : 0;
            if (!restoring)
                run.hasBackground = script && instruction >= script;
            if (!script || !run.hasBackground)
                return;

            const auto copy = [restoring](auto& state, uintptr_t address) {
                if (restoring)
                    std::memcpy(reinterpret_cast<void*>(address), state.data(), sizeof(state));
                else
                    std::memcpy(state.data(), reinterpret_cast<const void*>(address), sizeof(state));
            };
            copy(run.backgroundTimers, background + 0x20);
            copy(run.backgroundInterpolators, background + 0x50);
            copy(run.backgroundCamera, background + 0x4274);
            auto& cursor = *reinterpret_cast<uintptr_t*>(background + 0x4c);
            auto& drawDistance = *reinterpret_cast<uint32_t*>(background + 0x308c);
            if (restoring) {
                // Native restart reallocates the STD file. Never reuse its old IP.
                cursor = script + run.backgroundInstruction;
                drawDistance = run.backgroundDrawDistance;
            } else {
                run.backgroundInstruction = cursor - script;
                run.backgroundDrawDistance = drawDistance;
            }
        }

        static void CheckpointPositions(const Checkpoint& checkpoint, uintptr_t vm, bool restoring)
        {
            const auto player = GetMemContent(PLAYER_PTR);
            const auto section = checkpoint.section;
            const auto boss = (IsMidbossCheckpoint(section) || section >= Boss1)
                ? GetMemContent(vm + 0x1014) : 0;
            const auto copyState = [restoring](auto& state, uintptr_t address) {
                auto memory = reinterpret_cast<void*>(address);
                if (restoring)
                    std::memcpy(memory, state.data(), sizeof(state));
                else
                    std::memcpy(state.data(), memory, sizeof(state));
            };

            if (!restoring) {
                run.hasPlayerPosition = player != 0;
                run.hasBossPosition = boss != 0;
            }
            if (player && run.hasPlayerPosition) {
                copyState(run.playerPosition, player + 0x5e0);
                copyState(run.playerFixedPosition, player + 0x5ec);
                if (restoring)
                    std::memcpy(reinterpret_cast<void*>(player + 0x5b0), run.playerPosition.data(), sizeof(run.playerPosition));
            }
            if (boss && run.hasBossPosition) {
                copyState(run.bossMovement, boss + 0x11f0);
                copyState(run.bossInterpolators, boss + 0x14e8);
                if (section >= Boss1)
                    copyState(run.bossInvulnerability, boss + 0x3fec);
            }
        }

        static void CheckpointItems(bool restoring)
        {
            const auto manager = GetMemContent(ITEM_MANAGER_PTR);
            if (!restoring) {
                run.items.clear();
                run.hasItemSnapshot = manager != 0;
            }
            if (!manager || !run.hasItemSnapshot)
                return;

            // The restore route may have spawned its own copies of carryover items.
            const auto releaseItem = reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x439c10);
            const auto endEffect = reinterpret_cast<void(__stdcall*)(int, int)>(0x47f140);
            auto item = manager + itemArrayOffset;
            for (size_t i = 0; i < itemCount; ++i, item += itemStride) {
                const int motion = GetMemContent<int>(item + 0xbf0);
                const int type = GetMemContent<int>(item + 0xbf4);
                if (!motion || !((type >= 1 && type <= 3) || type == bombFragmentItem))
                    continue;
                if (restoring) {
                    if (const int effect = GetMemContent<int>(item + 0xba8))
                        endEffect(effect, 1);
                    releaseItem(item);
                } else {
                    run.items.push_back({
                        GetMemContent<std::array<float, 3>>(item + 0xbac),
                        GetMemContent<std::array<float, 3>>(item + 0xbb8),
                        GetMemContent<std::array<uint32_t, 3>>(item + 0xbc8),
                        motion, type, GetMemContent<float>(item + 0xbfc), GetMemContent<int>(item + 0xc00)
                    });
                }
            }
            if (!restoring)
                return;

            // A fresh native pool allocates slots in descending order.
            for (auto saved = run.items.rbegin(); saved != run.items.rend(); ++saved) {
                // A nonzero spawn delay suppresses the reward sound and popup.
                // Restore the real delay immediately after the game allocates its VMs.
                const auto spawned = spawnItem(manager, saved->type, saved->position.data(), 0, 0.0f, 0.0f, 1);
                if (!spawned)
                    break;
                std::memcpy(reinterpret_cast<void*>(spawned + 0xbac), saved->position.data(), sizeof(saved->position));
                std::memcpy(reinterpret_cast<void*>(spawned + 0xbb8), saved->velocity.data(), sizeof(saved->velocity));
                std::memcpy(reinterpret_cast<void*>(spawned + 0xbc8), saved->timer.data(), sizeof(saved->timer));
                *reinterpret_cast<int*>(spawned + 0xbf0) = saved->motion;
                *reinterpret_cast<float*>(spawned + 0xbfc) = saved->homingSpeed;
                *reinterpret_cast<int*>(spawned + 0xc00) = saved->delay;
            }
        }

        static void Reach(const Checkpoint& checkpoint, uintptr_t vm)
        {
            const bool restoring = run.retry == Retry::Restoring;
            if (restoring ? &checkpoint != run.checkpoint
                          : run.checkpoint && checkpoint.section <= run.checkpoint->section)
                return;

            CheckpointPositions(checkpoint, vm, restoring);
            if (checkpoint.section == Mid2 || checkpoint.section >= Card1) {
                // BossItemCard/Phase reads these flags from the preceding phase.
                auto& itemFlag = *reinterpret_cast<uint32_t*>(GetMemAddr(ENEMY_MANAGER_PTR, 0x18));
                const auto enemy = GetMemContent(vm + 0x1014);
                auto& enemyFlags = *reinterpret_cast<uint8_t*>(enemy + 0x5247);
                if (restoring) {
                    itemFlag = run.bossItemFlag;
                    enemyFlags = (enemyFlags & 0xfe) | run.bossTimeout;
                    if (&checkpoint != run.practiceEntry && checkpoint.health) {
                        // Match opcode 511's HP fields when the story threshold differs.
                        auto hp = reinterpret_cast<int32_t*>(enemy + 0x5164);
                        hp[0] = hp[1] = hp[2] = checkpoint.health;
                        hp[3] = checkpoint.health * 7;
                    }
                    if (&checkpoint != run.practiceEntry && checkpoint.scene)
                        reinterpret_cast<void(__stdcall*)(int)>(0x410470)(checkpoint.scene); // STD event
                } else {
                    run.bossItemFlag = itemFlag;
                    run.bossTimeout = enemyFlags & 1;
                }
            }

            // Restore after the retry route's STD events, which otherwise reset
            // the camera script to the beginning of a boss scene.
            CheckpointBackground(restoring);
            if (restoring) {
                RestoreStageTitle(stages[run.stage - 1]);
            } else {
                run.titlePending = run.titleVm != 0;
                if (run.titlePending)
                    run.titleTime = GetMemContent<float>(run.titleVm);
            }
            for (size_t i = 0; i < run.resources.size(); ++i) {
                auto& resource = *reinterpret_cast<uint32_t*>(resourceAddresses[i]);
                if (restoring)
                    resource = run.resources[i];
                else
                    run.resources[i] = resource;
            }
            if (restoring)
                chapterCounts = run.chapterCounts;
            else
                run.chapterCounts = chapterCounts;
            CheckpointItems(restoring);
            if (restoring) {
                // Stage initialization drew the counters before the checkpoint
                // restored their values. Refresh both rows, including fragments.
                if (const auto hud = GetMemContent(0x4db550)) {
                    using UpdateCounter = void(__thiscall*)(uintptr_t, int, int);
                    reinterpret_cast<UpdateCounter>(0x434ef0)(hud, GetMemContent<int>(0x4f5864), GetMemContent<int>(0x4f5868));
                    reinterpret_cast<UpdateCounter>(0x434fd0)(hud, GetMemContent<int>(0x4f5870), GetMemContent<int>(0x4f5874));
                }
            }
            if (checkpoint.section < Boss1)
                StageMusicAtCheckpoint(restoring);
            if (!restoring) {
                if (run.checkpoint || run.clearedPreviousStage)
                    run.chapterFinishStartedAt = GetTickCount64();
                if (!run.checkpoint && thPracParam.mode == 1)
                    run.practiceEntry = &checkpoint;
                run.clearedPreviousStage = false;
            }
            run.checkpoint = &checkpoint;
            if (restoring) {
                FinishRetryMusic(checkpoint.section < Boss1 && !run.holdMidbossMusic);
                SetRetryPhase(Retry::Ready);
            }
        }

        static void PlanRetry(const Stage& stage)
        {
            const auto& checkpoint = *run.checkpoint;
            const auto section = checkpoint.section;
            if (section >= Boss1)
                run.jumps[0] = { { 0, stage.front, 11 }, stage.boss, 60 };
            else if (section >= Latter1)
                run.jumps[0] = { { 0, stage.front, 11 }, stage.latter, 60 };
            else if (section >= Mid1)
                run.jumps[0] = { { 0, stage.front, 11 }, stage.midboss, 60 };
            run.jumps[1] = checkpoint.phaseJump;

            if (run.stage == 4 && section >= Boss1) {
                // These actors are part of the skipped pre-boss dialogue.
                run.jumps[2] = checkpoint.file == 4 ? Jump { { 4, 0x9950, 515 }, 0x9ab4 }
                                                    : Jump { { 5, 0x4dcc, 515 }, 0x4f30 };
            }
        }

        static uintptr_t Address(Instruction instruction)
        {
            return run.files[instruction.file] + instruction.offset;
        }

        static uint16_t& OpcodeAt(Instruction instruction)
        {
            return *reinterpret_cast<uint16_t*>(Address(instruction) + 4);
        }

        static Instruction Entry(const Checkpoint& checkpoint)
        {
            return { checkpoint.file, checkpoint.offset, 40 };
        }

        template <typename Action>
        static void ForEachMarker(const Stage& stage, Action action)
        {
            action(Instruction { 0, stage.titleWait, 23 });
            action(Instruction { 0, stage.titleWait + 0x14, 554 });
            for (const auto& checkpoint : stage.checkpoints)
                action(Entry(checkpoint));
            for (const auto& instruction : stage.midbossExits)
                action(instruction);
            for (const auto& jump : run.jumps) {
                if (jump.destination)
                    action(jump.from);
            }
        }

        static void InitStage(PracticeEdits edits = {})
        {
            const auto stageNumber = GetMemContent<unsigned>(STAGE_NUM);
            if (!IsStoryStage(stageNumber)) {
                Reset();
                return;
            }

            const bool retrying = run.retry == Retry::Requested && run.stage == stageNumber;
            if (!retrying) {
                const bool clearedPreviousStage = run.checkpoint && stageNumber == run.stage + 1;
                Reset();
                run.clearedPreviousStage = clearedPreviousStage;
            }
            run.practiceEdits = std::move(edits);
            run.stage = stageNumber;
            const auto& stage = stages[stageNumber - 1];

            const auto manager = GetMemContent(ENEMY_MANAGER_PTR);
            const auto loader = manager ? GetMemContent(manager + 0xcc) : 0;
            if (!loader) {
                run.bossRetryMusic = BossRetryMusic::None;
                SetRetryPhase(Retry::Playing);
                return;
            }
            const auto fileCount = stageNumber == 4 ? 6u : 4u;
            for (unsigned i = 0; i < fileCount; ++i)
                run.files[i] = GetMemContent(loader + 0xc + i * sizeof(uintptr_t));

            if (retrying) {
                const bool practiceEntry = run.checkpoint == run.practiceEntry;
                if (!practiceEntry)
                    PlanRetry(stage);
                if (run.checkpoint->section >= Boss1 && (!practiceEntry || !thPracParam.dlg)) {
                    const auto wait = stage.bossEntranceWaits[stage.bossEntranceWaits[0].file != run.checkpoint->file];
                    run.jumps[3] = { wait, wait.offset + 0x14 }; // Skip only the entrance's ins_23.
                }
            }

            bool valid = true;
            ForEachMarker(stage, [&](Instruction instruction) {
                valid = valid && run.files[instruction.file]
                    && (!run.practiceEdits.empty() || OpcodeAt(instruction) == instruction.opcode);
            });
            if (!valid) {
                run.files = {};
                run.jumps = {};
                run.bossRetryMusic = BossRetryMusic::None;
                SetRetryPhase(Retry::Playing);
                return;
            }

            // Opcode 93 has a dedicated VM case. Only tagged instruction addresses
            // dispatch into this code, and each tag is restored before execution.
            ForEachMarker(stage, [](Instruction instruction) {
                auto& opcode = OpcodeAt(instruction);
                if (opcode == instruction.opcode)
                    opcode = markerOpcode;
            });
            if (retrying) {
                run.stageLoadComplete = false;
                SetRetryPhase(Retry::Restoring);
                run.restoreTitle = true;
            }
        }

        static void UnpatchStage()
        {
            // The native restart can keep the loaded ECL. Undo unvisited markers
            // before the enemy manager is reset or destroyed.
            if (run.files[0]) {
                ForEachMarker(stages[run.stage - 1], [](Instruction instruction) {
                    auto& opcode = OpcodeAt(instruction);
                    if (opcode == markerOpcode)
                        opcode = instruction.opcode;
                });
            }
            for (auto edit = run.practiceEdits.rbegin(); edit != run.practiceEdits.rend(); ++edit)
                *edit->first = edit->second;
            run.practiceEdits.clear();
            run.chapterFinishStartedAt = 0;
            run.files = {};
            run.jumps = {};
            run.waitingForMidboss = nullptr;
            run.titleVm = 0;
            if (run.retry != Retry::Requested) {
                run.bossRetryMusic = BossRetryMusic::None;
                SetRetryPhase(Retry::Playing);
            }
        }

        static void EclMarker(PCONTEXT ctx)
        {
            if (!run.files[0])
                return;

            const auto& stage = stages[run.stage - 1];
            auto& ip = *reinterpret_cast<uintptr_t*>(ctx->Edi + 4);
            const auto execute = [&](Instruction instruction) {
                OpcodeAt(instruction) = instruction.opcode;
                ctx->Eip = 0x4823c0; // ECL dispatch
            };

            if (ip == run.files[0] + stage.titleWait) {
                execute({ 0, stage.titleWait, 23 });
                run.titleVm = ctx->Edi;
                if (run.restoreTitle) {
                    ip += 0x14;
                    ctx->Eip = 0x483c2b; // Continue with the VM's time check.
                    if (run.retry == Retry::Restoring) {
                        // Keep the title waiting while the checkpoint is rebuilt.
                        *reinterpret_cast<float*>(ctx->Edi) = -1000000.0f;
                    } else {
                        RestoreStageTitle(stage);
                    }
                }
                return;
            }
            if (ip == run.files[0] + stage.titleWait + 0x14) {
                execute({ 0, stage.titleWait + 0x14, 554 });
                run.titleVm = 0;
                return;
            }

            // A retry jump can be installed at a checkpoint for an earlier phase.
            for (const auto& jump : run.jumps) {
                if (jump.destination && ip == Address(jump.from)) {
                    execute(jump.from);
                    ip = run.files[jump.from.file] + jump.destination;
                    *reinterpret_cast<float*>(ctx->Edi) = jump.time;
                    return;
                }
            }
            for (const auto& checkpoint : stage.checkpoints) {
                const auto instruction = Entry(checkpoint);
                if (ip != Address(instruction))
                    continue;

                execute(instruction);
                if (run.retry == Retry::Requested)
                    return;
                if (checkpoint.section >= MidWave && checkpoint.section <= Latter2
                    && GetMemContent(ENEMY_MANAGER_PTR, 0x5c)) {
                    // The stage can start scheduling waves before the midboss leaves.
                    run.waitingForMidboss = &checkpoint;
                } else {
                    Reach(checkpoint, ctx->Edi);
                    if (run.retry == Retry::Ready)
                        ctx->Eip = 0x483cf6; // Yield the VM, leaving the entry instruction pending.
                }
                return;
            }
            for (const auto& instruction : stage.midbossExits) {
                if (ip == Address(instruction)) {
                    execute(instruction);
                    if (run.waitingForMidboss && run.retry != Retry::Requested)
                        Reach(*run.waitingForMidboss, ctx->Edi);
                    run.waitingForMidboss = nullptr;
                    if (run.retry == Retry::Ready)
                        ctx->Eip = 0x483cf6;
                    return;
                }
            }
        }

        static void TrackStageLoad(uint32_t flags)
        {
            // The stage can defer setup until frame 30. Let it finish resetting
            // the player, items, enemies and music before restoring a checkpoint.
            if (run.retry == Retry::Restoring)
                run.stageLoadComplete = !(flags & 0x800);
        }

        static void FastForwardStageStart(uintptr_t vm)
        {
            if (run.retry != Retry::Restoring || !run.stageLoadComplete || run.checkpoint->section >= Boss1)
                return;

            const auto& stage = stages[run.stage - 1];
            const auto firstWave = run.files[0] + stage.front;
            if (GetMemContent(vm + 4) == firstWave) {
                // MainFront and the retry destinations share the same ECL time.
                auto& time = *reinterpret_cast<float*>(vm);
                time = std::max(time, 60.0f);
            }
        }

        static bool RequestRetry(PCONTEXT ctx)
        {
            const auto stageNumber = GetMemContent<unsigned>(STAGE_NUM);
            if (run.retry != Retry::Playing || !run.checkpoint || !run.files[0]
                || run.stage != stageNumber || !IsStoryStage(stageNumber))
                return false;

            const auto section = run.checkpoint->section;
            run.bossRetryMusic = BossRetryMusic::None;
            if (section >= Boss1)
                run.bossRetryMusic = IsRetryTrackPlaying(1) ? BossRetryMusic::Keep : BossRetryMusic::Start;
            const auto enemies = GetMemContent(ENEMY_MANAGER_PTR);
            run.holdMidbossMusic = section < Boss1 && (IsMidbossCheckpoint(section)
                || (enemies && GetMemContent(enemies + 0x5c)));
            run.deferredStageMusic = run.holdMidbossMusic;
            run.seekStageMusic = false;
            run.restartShownAt = 0;
            SetRetryPhase(Retry::Covering);
            if (run.holdMidbossMusic) {
                // Native pause can skip a busy streaming buffer. Stop all BGM
                // buffers now. The checkpoint track is restarted after GO.
                const auto player = GetMemContent(0x4fd180);
                if (player)
                    reinterpret_cast<void(__thiscall*)(uintptr_t, int)>(0x481280)(player, 0);
            }
            // Hold the current frame until Restart has been drawn, then queue
            // the native restart. Suppress the life loss while the request waits.
            ctx->Eax = GetMemContent<DWORD>(0x4f5864);
            ctx->EFlags &= ~0x80u; // the following JS checks the original DEC's sign
            return true;
        }

        static void SelectRestartStage(PCONTEXT ctx)
        {
            if (run.retry != Retry::Requested)
                return;

            // The original instruction reads the run's starting stage. Supply
            // the checkpoint stage while leaving the original start stage intact.
            ctx->Eax = run.stage;
            ctx->Eip = 0x447815;
        }

        static bool RouteStageMusic(int& slot)
        {
            // Native delayed stage loading can run after the boss checkpoint is restored.
            if (run.bossRetryMusic != BossRetryMusic::None) {
                slot = 1;
                return run.bossRetryMusic == BossRetryMusic::Keep;
            }
            if (run.retry != Retry::Restoring)
                return false;

            slot = 0;
            run.deferredStageMusic = run.holdMidbossMusic || run.stageMusicPosition >= 0.001;
            return run.deferredStageMusic;
        }

        static bool IsStageLoadMusic(uintptr_t caller)
        {
            return caller == 0x4369a1 || caller == 0x436b83;
        }

        static bool KeepMusicDuringRebuild()
        {
            return run.bossRetryMusic == BossRetryMusic::Keep
                && (run.retry == Retry::Covering || RetryRebuilding());
        }

        static bool KeepRetryMusicForPlay(int slot, uintptr_t caller)
        {
            return run.bossRetryMusic == BossRetryMusic::Keep
                && (IsStageLoadMusic(caller) || (slot == 1 && KeepMusicDuringRebuild()));
        }

        static bool KeepRetryMusicForStop(uintptr_t caller)
        {
            return run.bossRetryMusic == BossRetryMusic::Keep
                && (caller == 0x436b74 || (caller == 0x435fe0 && KeepMusicDuringRebuild()));
        }

        static bool SuppressRetryMusicCommand(PCONTEXT ctx)
        {
            const auto args = reinterpret_cast<int32_t*>(ctx->Esp);
            switch (args[0]) {
            case 0x44572f: // PlayBGM queues a WAV stop before play.
                return KeepRetryMusicForPlay(GetMemContent<int>(ctx->Ebp + 8), GetMemContent(ctx->Ebp + 4));
            case 0x445743: {
                const auto caller = GetMemContent(ctx->Ebp + 4);
                return (IsStageLoadMusic(caller) && RouteStageMusic(args[2]))
                    || KeepRetryMusicForPlay(args[2], caller);
            }
            case 0x445836:
            case 0x44582c:
                return KeepRetryMusicForStop(args[4]);
            default:
                return false;
            }
        }
    }

    static bool IsSpellPractice()
    {
        return thPracParam.mode == 2 || (GetMemContent<unsigned>(0x4f58b8) & 0x30) == 0x20;
    }

    static uintptr_t __fastcall SpawnBombFragmentPair(uintptr_t manager, void*, int itemType,
        const float* position, uint32_t unused, float angle, float speed, int delay)
    {
        constexpr float angleOffset = 0.18f;
        const auto first = spawnItem(manager, bombFragmentItem, position, unused, angle - angleOffset, speed, delay);
        if (first) {
            // The native spawner advances the cycle only for a successfully created type 12 item.
            if (itemType == cycleRewardItem)
                ++*reinterpret_cast<int*>(BONUS_TOTAL);
            spawnItem(manager, bombFragmentItem, position, unused, angle + angleOffset, speed, delay);
        }
        return first;
    }

    static ImTextureID LoadHudImage(int resourceId)
    {
        const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
        const auto resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
        if (!resource)
            return {};
        const auto data = static_cast<const char*>(LockResource(LoadResource(module, resource)));
        if (!data)
            return {};
        return ReadImage(9, GetMemContent<DWORD>(0x4d8f68), nullptr, data, SizeofResource(module, resource));
    }

    static constexpr LONG retryLeft = 32, retryRight = 416, retryTop = 16, retryBottom = 464;
    static constexpr float promptCenterX = 224.0f, promptCenterY = 218.0f;

    static void RenderRetry(ImDrawList* draw)
    {
        using namespace Pointdevice;
        const auto phase = run.retry;
        if (phase == Retry::Playing && (!pointdeviceRun || !GetMemContent(PLAYER_PTR)))
            return;

        const auto now = GetTickCount64();
        static const auto background = LoadHudImage(IDR_TH14_RETRY_BACKGROUND);
        static const auto labels = LoadHudImage(IDR_TH14_RETRY_LABELS);
        static const auto sweep = LoadHudImage(IDR_TH14_RETRY_SWEEP);
        static const auto glow = LoadHudImage(IDR_TH14_RETRY_GLOW);
        if (phase == Retry::Playing)
            return; // Load the textures on entry to gameplay, before the first Miss.
        const auto screen = ImGui::GetIO().DisplaySize;
        const auto position = [&](float x, float y) {
            return ImVec2(x * screen.x / 640.0f, y * screen.y / 480.0f);
        };
        const auto tint = [](float opacity) {
            return IM_COL32(255, 255, 255, static_cast<int>(opacity * 255.0f + 0.5f));
        };

        const auto elapsed = RetryElapsed(now);
        run.retryFrameDrawn = true;
        draw->PushClipRect(position(retryLeft, retryTop), position(retryRight, retryBottom), true);

        struct SpriteBounds {
            float left, top, right, bottom;
        };
        // Pixel bounds in the original TH15 front03.png.
        constexpr SpriteBounds ready { 6, 2, 188, 63 };
        constexpr SpriteBounds go { 2, 75, 125, 118 };
        constexpr SpriteBounds restart { 24, 146, 230, 189 };
        const auto& sprite = phase == Retry::Ready ? ready : phase == Retry::Go ? go : restart;
        float opacity = 1.0f;
        float scale = 1.0f;
        if (phase == Retry::Ready) {
            opacity = RetryPulseOpacity(elapsed, readyPulseMs);
        } else if (phase == Retry::Go) {
            const float arrival = EaseRetry(elapsed / 90.0f);
            const float fade = 1.0f - EaseRetry((static_cast<float>(elapsed) - 200.0f) / (goDurationMs - 200.0f));
            opacity = arrival * fade;
            scale = 0.82f + 0.27f * arrival - 0.09f * EaseRetry(elapsed / 180.0f);
            const float radius = 23.0f + 33.0f * EaseRetry(elapsed / 300.0f);
            if (glow) {
                draw->AddImage(glow, position(promptCenterX - radius, promptCenterY - radius),
                    position(promptCenterX + radius, promptCenterY + radius), { 0, 0 }, { 1, 1 }, tint(0.55f * opacity));
            }
        } else {
            const float coverage = phase == Retry::Covering
                ? EaseRetry(static_cast<float>(elapsed) / coverInMs) : 1.0f;
            run.coverFrameDrawn = phase == Retry::Covering && elapsed >= coverInMs;
            const float edge = retryTop + (retryBottom - retryTop) * coverage;
            if (coverage > 0.0f) {
                if (background)
                    draw->AddImage(background, position(retryLeft, retryTop), position(retryRight, edge), { 0, 0 }, { 1, coverage });
                else
                    draw->AddRectFilled(position(retryLeft, retryTop), position(retryRight, edge), IM_COL32(40, 19, 53, 255));
            }
            if (sweep && coverage > 0.0f && coverage < 1.0f) {
                const float intensity = 2.8f * coverage * (1.0f - coverage);
                draw->AddImage(sweep, position(retryLeft, edge - 13.0f), position(retryRight, edge + 13.0f),
                    { 0, 0 }, { 1, 1 }, IM_COL32(223, 192, 255, static_cast<int>(255.0f * intensity)));
            }
            opacity = EaseRetry((coverage - 0.52f) / 0.34f) * RestartOpacity(now);
        }

        if (labels && opacity > 0.0f) {
            const float halfWidth = (sprite.right - sprite.left) * scale / 4.0f;
            const float halfHeight = (sprite.bottom - sprite.top) * scale / 4.0f;
            draw->AddImage(labels, position(promptCenterX - halfWidth, promptCenterY - halfHeight),
                position(promptCenterX + halfWidth, promptCenterY + halfHeight),
                { sprite.left / 256.0f, sprite.top / 192.0f },
                { sprite.right / 256.0f, sprite.bottom / 192.0f }, tint(opacity));
        }
        draw->PopClipRect();
    }

    static Microsoft::WRL::ComPtr<IDirect3DSurface9> retryBackdrop;

    static void CaptureRetryBackdrop(IDirect3DDevice9* device)
    {
        Microsoft::WRL::ComPtr<IDirect3DSurface9> backBuffer;
        D3DSURFACE_DESC desc {};
        if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, backBuffer.GetAddressOf()))
            || FAILED(backBuffer->GetDesc(&desc))
            || FAILED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, desc.MultiSampleType,
                desc.MultiSampleQuality, FALSE, retryBackdrop.ReleaseAndGetAddressOf(), nullptr)))
            return;
        if (FAILED(device->StretchRect(backBuffer.Get(), nullptr, retryBackdrop.Get(), nullptr, D3DTEXF_NONE)))
            retryBackdrop.Reset();
    }

    static bool RestoreRetryHud(IDirect3DDevice9* device)
    {
        Microsoft::WRL::ComPtr<IDirect3DSurface9> backBuffer;
        D3DSURFACE_DESC desc {};
        if (!retryBackdrop
            || FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, backBuffer.GetAddressOf()))
            || FAILED(backBuffer->GetDesc(&desc)))
            return false;

        // Preserve the frame outside the playfield while the native HUD rebuilds.
        // The newly drawn playfield can then show the current Restart pulse.
        const auto width = static_cast<LONG>(desc.Width), height = static_cast<LONG>(desc.Height);
        const LONG left = retryLeft * width / 640, right = retryRight * width / 640;
        const LONG top = retryTop * height / 480, bottom = retryBottom * height / 480;
        const RECT margins[] {
            { 0, 0, width, top }, { 0, bottom, width, height },
            { 0, top, left, bottom }, { right, top, width, bottom },
        };
        for (const auto& margin : margins) {
            if (FAILED(device->StretchRect(retryBackdrop.Get(), &margin, backBuffer.Get(), &margin, D3DTEXF_NONE)))
                return false;
        }
        return true;
    }

    static void PresentRetryFrame(PCONTEXT ctx, bool clearOnly = false)
    {
        using namespace Pointdevice;
        const bool rebuilding = RetryRebuilding();
        if (!rebuilding && !run.coverFrameDrawn)
            retryBackdrop.Reset();
        if (run.retry == Retry::Playing)
            return;

        const auto device = GetMemContent<IDirect3DDevice9*>(0x4d8f68);
        const auto now = GetTickCount64();
        if (clearOnly || (rebuilding && (!run.retryFrameDrawn || !RestoreRetryHud(device)))) {
            ctx->Eax = clearOnly ? D3D_OK : device->TestCooperativeLevel();
            RetryPresented(now, false);
        } else {
            if (run.coverFrameDrawn)
                CaptureRetryBackdrop(device);
            const auto result = device->Present(nullptr, nullptr, nullptr, nullptr);
            ctx->Eax = result;
            RetryPresented(GetTickCount64(), SUCCEEDED(result));
        }
        // The five D3D9 Present arguments have already been pushed at each site.
        ctx->Esp += 5 * sizeof(DWORD);
        ctx->Eip += 3;
    }

    static void RenderDisabledLifeSlot(ImDrawList* draw)
    {
        if (!pointdeviceRun || !GetMemContent(PLAYER_PTR) || THGuiRep::singleton().mRepStatus)
            return;
        const auto hud = GetMemContent(0x4db550);
        if (!hud || !GetMemContent(hud + 0x58))
            return;

        static const auto texture = LoadHudImage(IDR_TH14_LIFE_DISABLED);
        if (!texture)
            return;

        // Match the rendered life row in 1280x960 space, accounting for PNG padding.
        constexpr float centerX = 1137.0f;
        constexpr float centerY = 220.0f;
        constexpr float width = 280.0f;
        constexpr float height = width * 84.0f / 430.0f;
        const auto screen = ImGui::GetIO().DisplaySize;
        const float x = screen.x / 1280.0f;
        const float y = screen.y / 960.0f;
        draw->AddImage(texture, { (centerX - width / 2) * x, (centerY - height / 2) * y },
            { (centerX + width / 2) * x, (centerY + height / 2) * y });
    }

    static void RenderChapterFinish(ImDrawList* draw)
    {
        const auto startedAt = Pointdevice::run.chapterFinishStartedAt;
        if (!startedAt)
            return;
        const float opacity = Pointdevice::ChapterFinishOpacity(GetTickCount64() - startedAt);
        if (opacity <= 0.0f)
            return;
        static const auto texture = LoadHudImage(IDR_TH14_CHAPTER_FINISH);
        if (!texture)
            return;

        // Chapter Finish cropped from TH15's front02.png.
        const auto screen = ImGui::GetIO().DisplaySize;
        const float x = screen.x / 640.0f, y = screen.y / 480.0f;
        constexpr float halfWidth = 171.0f * 0.8f / 2, halfHeight = 31.0f * 0.8f / 2;
        draw->AddImage(texture, { (promptCenterX - halfWidth) * x, (promptCenterY - halfHeight) * y },
            { (promptCenterX + halfWidth) * x, (promptCenterY + halfHeight) * y },
            { 0, 0 }, { 1, 1 }, IM_COL32(255, 255, 255, static_cast<int>(opacity * 255.0f + 0.5f)));
    }

    int THBGMTest()
    {
        if (!thPracParam.mode)
            return 0;
        else if (thPracParam.section >= 10000)
            return 0;
        else if (thPracParam.dlg)
            return 0;
        else
            return th_sections_bgm[thPracParam.section];
    }
    void THSaveReplay(char* repName)
    {
        ReplaySaveParam(mb_to_utf16(repName, 932).c_str(), thPracParam.GetJson());
    }

    static void RenderLockTimer(ImDrawList* p)
    {
        if (g_lock_timer_flag) {
            g_lock_timer++;
            g_lock_timer_flag = false;
        }

        if (g_adv_igi_options.enable_lock_timer_autoly && *THOverlay::singleton().mTimeLock) {
            std::string time_text = std::format("{:.2f}", (float)g_lock_timer / 60.0f);
            auto sz = ImGui::CalcTextSize(time_text.c_str());
            p->AddRectFilled({ 64.0f, 0.0f }, { 220.0f, sz.y }, 0xFFFFFFFF);
            p->AddText({ 220.0f - sz.x, 0.0f }, 0xFF000000, time_text.c_str());
        }
    }

    void RenderBtHitbox(ImDrawList* p)
    {
        // show bullet hitbox
        if (g_show_bullet_hitbox) {
            DWORD ppl = *(DWORD*)0x4DB67C;
            if ((thPracParam.mode == 1 || THGuiRep::singleton().mRepStatus) && ppl) {
                ImGuiIO& io = ImGui::GetIO();
                float x_ratio = io.DisplaySize.x / 1280.0f;
                float y_ratio = io.DisplaySize.y / 960.0f;
                p->PushClipRect({ 64.0f * x_ratio, 32.0f * y_ratio }, { 832.0f * x_ratio, 928.0f * y_ratio });
                auto Multiply = [](ImVec2 a, ImVec2 b) -> ImVec2 {
                    return { a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x };
                };
                auto Add = [](ImVec2 a, ImVec2 b) -> ImVec2 {
                    return { a.x + b.x, a.y + b.y };
                };
                auto GetClientFromStage = [&](ImVec2 stage) -> ImVec2 {
                    float x = stage.x;
                    float y = stage.y;

                    y = y * 2.0f + 32.0f;
                    x = x * 2.0f + 448.0f;
                    return { x * x_ratio, y * y_ratio };
                };

                // bts
                DWORD psht = *(DWORD*)(ppl + 0x688);
                if (psht) {
                    float plhit = *(float*)(*(DWORD*)(ppl + 0x688) + 4);
                    if ((*(BYTE*)(*(DWORD*)(0x4DB67C) + 0x182D4) & 0x10) != 0) // bigger
                        plhit = (float)(*(float*)(*(DWORD*)(0x4DB67C) + 0x18308) * 3.5999999) * plhit;
                    DWORD pbtx = *(DWORD*)(0x4DB530);
                    if (pbtx) {
                        DWORD iter = *(DWORD*)(pbtx + 0x80);
                        while (iter) {
                            DWORD pbt = *(DWORD*)(iter);
                            ImVec2 pos = *(ImVec2*)(pbt + 0xBC0);
				            DWORD v3 = *(DWORD*)(pbt + 0x20);
                            if ((v3 & 2) != 0) {
                                if ((v3 & 0x40) != 0) {
                                    if ((v3 & 0x10) == 0) {
                                        //rc
                                    } else {
                                        float radius = *(float*)(pbt + 0x13BC) * *(float*)(pbt + 0xBE0);
                                        float bt_hit2 = sqrt(radius * radius + plhit * plhit);
                                        auto pos2 = GetClientFromStage(pos);
                                        p->AddCircleFilled(pos2, bt_hit2 * x_ratio*2.0f, 0x6622DDCC); // AGBR
                                        p->AddCircle(pos2, bt_hit2 * x_ratio * 2.0f, 0xCCFF00FF); // AGBR
                                        iter = *(DWORD*)(iter + 4);
                                    }
                                } else if ((v3 & 0x10) != 0) {
                                    float radius = *(float*)(pbt + 0xBE0);
                                    float bt_hit2 = sqrt(radius * radius + plhit * plhit);
                                    auto pos2 = GetClientFromStage(pos);
                                    p->AddCircleFilled(pos2, bt_hit2 * x_ratio * 2.0f, 0x6622DDCC); // AGBR
                                    p->AddCircle(pos2, bt_hit2 * x_ratio * 2.0f, 0xCCFF00FF); // AGBR
                                    iter = *(DWORD*)(iter + 4);
                                } else {
                                    //rc
                                }
                            }

                        }
                    }
                }

                // atts
                DWORD p_arr = ppl + 0xDE18;
                for (int i = 0; i < 256; i++)
                {
                    DWORD p_att = p_arr + 0xA4 * i;
                    DWORD flag = *(DWORD*)(p_att + 0x10);
                    if (flag & 1)
                    {
                        if (flag & 2)
                        {//circle
                            float radius = *(float*)(p_att + 0x14);
                            ImVec2 pos = *(ImVec2*)(p_att + 0x2C);
                            auto pos2 = GetClientFromStage(pos);
                            p->AddCircleFilled(pos2, radius * x_ratio * 2.0f, 0x5522DD55); // AGBR
                            p->AddCircle(pos2, radius * x_ratio * 2.0f, 0xCCFFFFFF);
                        }
                        else
                        {//rect
                            float width = *(float*)(p_att + 0x24);
                            float height = *(float*)(p_att + 0x28);
                            float angle = *(float*)(p_att + 0x1C);
                            ImVec2 pos = *(ImVec2*)(p_att + 0x2C);

                            ImVec2 posA = { width * 0.5f, -height * 0.5f };
                            ImVec2 posB = { width * 0.5f, height * 0.5f };
                            ImVec2 posC = { -width * 0.5f, -height * 0.5f };
                            ImVec2 posD = { -width * 0.5f, height * 0.5f };
                            ImVec2 rotation = { cosf(angle), sinf(angle) };
                            posA = Multiply(posA, rotation);
                            posB = Multiply(posB, rotation);
                            posC = Multiply(posC, rotation);
                            posD = Multiply(posD, rotation);
                            posA = GetClientFromStage(Add(posA, pos));
                            posB = GetClientFromStage(Add(posB, pos));
                            posC = GetClientFromStage(Add(posC, pos));
                            posD = GetClientFromStage(Add(posD, pos));
                            p->AddQuadFilled(posA, posB, posD, posC, 0x5522DD55);
                            p->AddQuad(posA, posB, posD, posC, 0xCCFFFFFF);
                        }
                    }
                }

                //enms
                DWORD enmBase = *(DWORD*)(0x4DB544);
                if (enmBase) {
                    DWORD iter = *(DWORD*)(enmBase + 0xD0);
                    while (iter) {
                        DWORD pEnm = *(DWORD*)(iter);
                        {
                            //+4054
                            DWORD flag = *(DWORD*)(pEnm + 0x11f0 + 0x4054);
                            ImVec2 pos = *(ImVec2*)(pEnm + 0x11f0 + 0x44);
                            if (*(DWORD*)(pEnm + 0x11f0 + 0x4114) == 0x42C9C0)
                            //cup
                            {
                                DWORD anm = *(DWORD*)(pEnm + 0x11f0 + 0x128);
                                DWORD pTex = asm_call<0x47F0A0, Thiscall, DWORD>(*(DWORD*)0x4F56CC, anm);
                                if (pTex)
                                {
                                    ImVec2 sz = *(ImVec2*)(pTex + 0x60);
                                    float angle = *(float*)(pTex + 0x50);

                                    sz.x *= 32.0f;
                                    sz.y *= 192.0f;

                                    ImVec2 pos2 = { pos.x, pos.y + 24.0f };
                                    ImVec2 p1 {- sz.y * 0.5f, - sz.x * 0.5f };
                                    ImVec2 p2 {+ sz.y * 0.5f, - sz.x * 0.5f };
                                    ImVec2 p3 {+ sz.y * 0.5f, + sz.x * 0.5f };
                                    ImVec2 p4 {- sz.y * 0.5f, + sz.x * 0.5f };

                                    ImVec2 A { cosf(angle), sinf(angle) };
                                    p1 = GetClientFromStage(Add(Multiply(p1, A), pos2));
                                    p2 = GetClientFromStage(Add(Multiply(p2, A), pos2));
                                    p3 = GetClientFromStage(Add(Multiply(p3, A), pos2));
                                    p4 = GetClientFromStage(Add(Multiply(p4, A), pos2));

                                    p->AddQuad(p1, p2, p3, p4, 0xFF00FF00);
                                    p->AddQuadFilled(p1, p2, p3, p4, 0x44FF00FF);

                                    pos2 = { pos2.x, pos2.y + 32.0f };
                                    float radius = 48.0f;
                                    p->AddCircle(GetClientFromStage(pos2), radius * x_ratio * 2.0f, 0xFF00FF00);
                                    p->AddCircleFilled(GetClientFromStage(pos2), radius * x_ratio * 2.0f, 0x44FF00FF);

                                }
                            }
                            if ((flag & 0x20) == 0)
                            {
                                if ((flag & 0x1000)){
                                    // rc
                                    ImVec2 hit = *(ImVec2*)(pEnm + 0x11f0 + 0x110);// bei dan
                                    ImVec2 att = *(ImVec2*)(pEnm + 0x11f0 + 0x118);// ti shu
                                    float angle = *(float*)(pEnm + 0x11f0 + 0x120);

                                    ImVec2 p1 { - att.y * 0.5f, - att.x * 0.5f };
                                    ImVec2 p2 { + att.y * 0.5f, - att.x * 0.5f };
                                    ImVec2 p3 { + att.y * 0.5f, + att.x * 0.5f };
                                    ImVec2 p4 { - att.y * 0.5f, + att.x * 0.5f };
                                    ImVec2 A { cosf(angle), sinf(angle) };
                                    p1 = GetClientFromStage(Add(Multiply(p1, A),pos));
                                    p2 = GetClientFromStage(Add(Multiply(p2, A),pos));
                                    p3 = GetClientFromStage(Add(Multiply(p3, A),pos));
                                    p4 = GetClientFromStage(Add(Multiply(p4, A),pos));
                                    if ((flag & 0x2) == 0) {
                                        p->AddQuad(p1, p2, p3, p4, 0xFF00FF00);
                                        p->AddQuadFilled(p1, p2, p3, p4, 0x44FF00FF);
                                    }
                                    p1 = { - hit.y * 0.5f, - hit.x * 0.5f };
                                    p2 = { + hit.y * 0.5f, - hit.x * 0.5f };
                                    p3 = { + hit.y * 0.5f, + hit.x * 0.5f };
                                    p4 = { - hit.y * 0.5f, + hit.x * 0.5f };
                                    p1 = GetClientFromStage(Add(Multiply(p1, A), pos));
                                    p2 = GetClientFromStage(Add(Multiply(p2, A), pos));
                                    p3 = GetClientFromStage(Add(Multiply(p3, A), pos));
                                    p4 = GetClientFromStage(Add(Multiply(p4, A), pos));
                                    if ((flag & 0x1) == 0) {
                                        p->AddQuad(p1, p2, p3, p4, 0xFFFF00FF);
                                        p->AddQuadFilled(p1, p2, p3, p4, 0x44FF00FF);
                                    }
                                }
                                else
                                {
                                    float radius = *(float*)(pEnm + 0x11f0 + 0x110)*0.5f;
                                    float radius2 = *(float*)(pEnm + 0x11f0 + 0x118) * 0.5f;
                                    if ((flag & 0x1) == 0) {
                                        p->AddCircle(GetClientFromStage(pos), radius * x_ratio * 2.0f, 0xFF00FF00);
                                        p->AddCircleFilled(GetClientFromStage(pos), radius * x_ratio * 2.0f, 0x4400FF00);
                                    }
                                    if ((flag & 0x2) == 0) {
                                        p->AddCircle(GetClientFromStage(pos), radius2 * x_ratio * 2.0f, 0xFFFF00FF);
                                        p->AddCircleFilled(GetClientFromStage(pos), radius2 * x_ratio * 2.0f, 0x44FF00FF);
                                    }
                                }
                            }
                        }
                        iter = *(DWORD*)(iter + 0x4);
                    }
                }

                p->PopClipRect();
            }
        }
    }

#pragma warning(push)
#pragma warning(disable : 4456)
    void RenderHitBar(ImDrawList* p)
    {
        int items = *(DWORD*)(0x4F5880);
        bool border = true;
        if (items == 0) {
            border = false;
            DWORD pitems = *(DWORD*)(0x4DB660);
            if (pitems) {
                DWORD iter = pitems + 0x14;
                for (int i = 0; i < 0x1258; i++) {
                    int type = *(DWORD*)(iter + 0xBF4);
                    int movement = *(DWORD*)(iter + 0xBF0);
                    if (movement != 0)
                        if (type == 1 || type == 2 || type == 3)
                            items++;
                    iter += 0xC18;
                }
            }
        }
        DWORD ppl = *(DWORD*)(0x004DB67C);
        if (ppl) {
            float num = 0.0f;
            DWORD col = 0xFFFFFFFF;
            DWORD col2 = 0xFF000000;
            if (items < 20) {
                num = items / 20.0f;
                col = 0xFF888888;
            } else if (items < 30) {
                num = (items - 20.0f) / 10.0f;
                col = 0xFF0000FF;
                col2 = 0xFF888888;
            } else if (items < 40) {
                num = (items - 30.0f) / 10.0f;
                col = 0xFF00FFCC;
                col2 = 0xFF0000FF;
            } else if (items < 50) {
                num = (items - 40.0f) / 10.0f;
                col = 0xFF00FF00;
                col2 = 0xFF00FFCC;
            } else if (items < 60) {
                num = (items - 50.0f) / 10.0f;
                col = 0xFFFFCC00;
                col2 = 0xFF00FF00;
            } else {
                num = 1.0f;
                col = 0xFFFFFF00;
                col2 = 0xFFFFCC00;
            }
            ImGuiIO& io = ImGui::GetIO();
            float x_ratio = io.DisplaySize.x / 1280.0f;
            float y_ratio = io.DisplaySize.y / 960.0f;

            float xpos = *(float*)(ppl + 0x5B0) * 2.0f * x_ratio + 448.0f * x_ratio;
            float ypos = *(float*)(ppl + 0x5B4) * 2.0f * y_ratio + 32.0f * y_ratio;
            p->PushClipRect({ 64.0f * x_ratio, 32.0f * y_ratio }, { 832.0f * x_ratio, 928.0f * y_ratio });
            const float bar_xszhalf = 48.0f * x_ratio;
            const float bar_yszhalf = 4.0f * y_ratio;
            const float bar_yofs = 48.0f * y_ratio;
            // st5 rev
            float stage_y_rev = 1.0f, stage_x_rev = 1.0f, stage_rotate = 0.0f;
            if (*(DWORD*)0x4D9128) {
                stage_y_rev = *(float*)((*(DWORD*)0x4D9128) + 0x64);
                stage_x_rev = *(float*)((*(DWORD*)0x4D9128) + 0x60);
                stage_rotate = *(float*)((*(DWORD*)0x4D9128) + 0x50);
            }
            auto GetXY_Revd = [stage_y_rev, stage_x_rev, stage_rotate, y_ratio, x_ratio](ImVec2 pos) -> ImVec2 {
                float &x = pos.x, &y = pos.y;
                float dx = x - 448.0f * x_ratio, dy = y - 480.0f * y_ratio;
                if (stage_rotate != 0.0f) {

                    float c = cosf(stage_rotate);
                    float s = sinf(stage_rotate);
                    float a = dx * c - dy * s;
                    float b = dx * s + dy * c;
                    dx = a;
                    dy = b;
                }
                dx *= stage_x_rev;
                dy *= stage_y_rev;
                return { dx + 448.0f * x_ratio, dy + 480.0f * y_ratio };
            };
            auto AddQuadFilled_Revd = [stage_y_rev, stage_x_rev, stage_rotate, y_ratio, x_ratio, p, GetXY_Revd](ImVec2 pmin, ImVec2 pmax, DWORD col) -> void {
                ImVec2 p3 = { pmax.x, pmin.y }, p4 = { pmin.x, pmax.y };
                p->AddQuadFilled(GetXY_Revd(pmin), GetXY_Revd(p3), GetXY_Revd(pmax), GetXY_Revd(p4), col);
            };
            auto AddQuad_Revd = [stage_y_rev, stage_x_rev, stage_rotate, y_ratio, x_ratio, p, GetXY_Revd](ImVec2 pmin, ImVec2 pmax, DWORD col) -> void {
                ImVec2 p3 = { pmax.x, pmin.y }, p4 = { pmin.x, pmax.y };
                p->AddQuad(GetXY_Revd(pmin), GetXY_Revd(p3), GetXY_Revd(pmax), GetXY_Revd(p4), col);
            };
            // shadow
            {
                ImVec2 pmin, pmax;
                if (items >= 20) {
                    pmin = { xpos - bar_xszhalf, ypos - bar_yofs - bar_yszhalf };
                    pmax = { pmin.x + bar_xszhalf * 2.0f, pmin.y + bar_yszhalf * 2.0f };
                } else {
                    pmin = { xpos - bar_xszhalf, ypos - bar_yofs - bar_yszhalf };
                    pmax = { pmin.x + bar_xszhalf * 2.0f * num, pmin.y + bar_yszhalf * 2.0f };
                }
                pmin.x += 1.0f;
                pmin.y += 1.0f;
                pmax.x += 1.0f;
                pmax.y += 1.0f;
                AddQuadFilled_Revd(pmin, pmax, col2);
            }
            {
                ImVec2 pmin = { xpos - bar_xszhalf, ypos - bar_yofs - bar_yszhalf };
                ImVec2 pmax = { pmin.x + bar_xszhalf * 2.0f * num, pmin.y + bar_yszhalf * 2.0f };
                AddQuadFilled_Revd(pmin, pmax, col);
                if (border) {
                    ImVec2 pmin = { xpos - bar_xszhalf, ypos - bar_yofs - bar_yszhalf };
                    ImVec2 pmax = { pmin.x + bar_xszhalf * 2.0f, pmin.y + bar_yszhalf * 2.0f };
                    AddQuad_Revd(pmin, pmax, 0xFFFFFF00);
                } else {
                    ImVec2 pmin = { xpos - bar_xszhalf, ypos - bar_yofs - bar_yszhalf };
                    ImVec2 pmax = { pmin.x + bar_xszhalf * 2.0f, pmin.y + bar_yszhalf * 2.0f };
                    AddQuad_Revd(pmin, pmax, 0xFFCCCCCC);
                }
            }
            p->PopClipRect();
        }
    }
#pragma warning(pop)

    HOOKSET_DEFINE(THMainHook)
    EHOOK_DY(th14_inf_lives, 0x0044F617,1,
    {
        if ((*(THOverlay::singleton().mInfLives))) {
            if (!g_adv_igi_options.map_inf_life_to_no_continue) {
                pCtx->Eax++;
            } else {
                if (*(DWORD*)(0x4F5864) == 0)
                    pCtx->Eax++;
            }
        }
    })
    EHOOK_DY(th14_everlasting_bgm, 0x46ef90, 1, {
        auto args = reinterpret_cast<int32_t*>(pCtx->Esp);
        const int32_t retn_addr = args[0];
        const int32_t bgm_cmd = args[1];
        auto& bgm_id = args[2];

        if (Pointdevice::SuppressRetryMusicCommand(pCtx)) {
            pCtx->Eip = 0x46f022;
            return;
        }

        bool el_switch;
        bool is_practice;
        bool result;

        el_switch = *(THOverlay::singleton().mElBgm) && !pointdeviceRun && !THGuiRep::singleton().mRepStatus && thPracParam.mode == 1 && thPracParam.section;
        is_practice = (*((int32_t*)0x4f58b8) & 0x1); // is restarting
        result = ElBgmTest<0x445743, 0x436526, 0x448e91, 0x44908a, 0xffffffff>(
            el_switch, is_practice, retn_addr, bgm_cmd, bgm_id, 0xffffffff);

        if (result) {
            pCtx->Eip = 0x46f022;
        }
    })
    EHOOK_DY(th14_pointdevice_bgm_stop, 0x436506, 7, {
        if (Pointdevice::KeepMusicDuringRebuild())
            pCtx->Eip = 0x43652d;
    })
    EHOOK_DY(th14_pointdevice_bgm_seek, 0x46e915, 5, {
        Pointdevice::SeekStageMusic(pCtx);
    })
    EHOOK_DY(th14_pointdevice_hold_midboss_bgm, 0x46e637, 2, {
        // This is the BGM queue consumer, including commands queued before Miss.
        if (Pointdevice::HoldMidbossMusicCommand(GetMemContent<int>(pCtx->Edi))) {
            pCtx->Ebx = 0;
            pCtx->Eip = 0x46e8cc; // Drop the command and advance the queue.
        }
    })
    EHOOK_DY(th14_param_reset, 0x45a111, 7, {
        thPracParam.Reset();
        Pointdevice::Reset();
        pointdeviceRun = false;
        pointdeviceLocked = false;
        THAdvOptWnd::singleton().ToggleLock(false);
    })
    EHOOK_DY(th14_prac_menu_1, 0x45e768, 7, {
        THGuiPrac::singleton().State(1);
        THAdvOptWnd::singleton().ToggleLock(false);
    })
    EHOOK_DY(th14_prac_menu_2, 0x45e787, 3, {
        THGuiPrac::singleton().State(2);
    })
    EHOOK_DY(th14_prac_menu_3, 0x45ea9f, 7, {
        THGuiPrac::singleton().State(3);
        pointdeviceRun = pointdeviceEnabled;
        pointdeviceLocked = true;
        THAdvOptWnd::singleton().ToggleLock(true);
    })
    EHOOK_DY(th14_prac_menu_4, 0x45eb40, 7, {
        THGuiPrac::singleton().State(4);
    })
    PATCH_DY(th14_prac_menu_enter_1, 0x45e847, "eb")
    EHOOK_DY(th14_prac_menu_enter_2, 0x45eafc, 1, {
        pCtx->Ecx = thPracParam.stage;
    })
    PATCH_DY(th14_disable_prac_menu_1, 0x45eca7, "eb3b")
    EHOOK_DY(th14_menu_rank_fix, 0x449c3d, 5, {
        *((int32_t*)0x4f5844) = -1;
        *((int32_t*)0x4f5834) = *((int32_t*)0x4d5984);
    })
    EHOOK_DY(th14_fake_type, 0x0042ABAF,3,
    {
        if (thPracParam.mode == 1 && thPracParam.faketype != 0 && thPracParam.stage == 3) {
            pCtx->Eax = thPracParam.faketype == 1?0:1;
        }
    })
    EHOOK_DY(th14_patch_main, 0x4360ce, 1, {
        const bool spellPractice = IsSpellPractice();
        if (!pointdeviceLocked && !THGuiRep::singleton().mRepStatus
            && (thPracParam.mode == 1 || spellPractice))
            pointdeviceRun = pointdeviceEnabled;
        pointdeviceLocked = true;
        PracticeEdits edits;
        if (thPracParam.mode == 1) {
            *(int32_t*)(0x4f5830) = (int32_t)(thPracParam.score / 10);
            *(int32_t*)(0x4f5864) = thPracParam.life;
            *(int32_t*)(0x4f5868) = thPracParam.life_fragment;
            *(int32_t*)(0x4f5870) = thPracParam.bomb;
            *(int32_t*)(0x4f5874) = thPracParam.bomb_fragment;
            *(int32_t*)(0x4f5894) = thPracParam.cycle;
            *(int32_t*)(0x4f5858) = thPracParam.power;
            *(int32_t*)(0x4f584c) = thPracParam.value * 100;
            *(int32_t*)(0x4f5840) = thPracParam.graze; // 0x4E7420: Chapter Graze

            const auto& run = Pointdevice::run;
            const auto stage = GetMemContent<unsigned>(STAGE_NUM);
            const bool track = Pointdevice::IsStoryStage(stage);
            const bool retrying = track && run.retry == Pointdevice::Retry::Requested && run.stage == stage;
            if (!retrying || run.checkpoint == run.practiceEntry)
                THSectionPatch(track ? &edits : nullptr);
        } else if (thPracParam.mode == 2) {
            PracticeECL ecl((void*)GetMemAddr(0x4db544, 0xcc, 0xC));
            THPatchSP(ecl);
        }
        if (!spellPractice)
            Pointdevice::InitStage(std::move(edits));
        else
            Pointdevice::Reset();
        thPracParam._playLock = true;
    })
    EHOOK_DY(th14_pointdevice_ecl_marker, 0x483b3a, 2, {
        Pointdevice::EclMarker(pCtx);
    })
    EHOOK_DY(th14_pointdevice_ecl_hold, 0x48238e, 2, {
        // Other VMs in the current enemy update can still run after the checkpoint.
        if (Pointdevice::run.retry == Pointdevice::Retry::Ready)
            pCtx->Eip = 0x483cf6;
        else
            Pointdevice::FastForwardStageStart(pCtx->Ecx);
    })
    EHOOK_DY(th14_pointdevice_stage_loaded, 0x436baa, 4, {
        Pointdevice::TrackStageLoad(GetMemContent<uint32_t>(pCtx->Edi + 0x80));
    })
    EHOOK_DY(th14_pointdevice_stage_exit, 0x4360e0, 1, {
        Pointdevice::UnpatchStage();
    })
    EHOOK_DY(th14_bgm, 0x43699a, 2, {
        if (THBGMTest()) {
            PushHelper32(pCtx, 1);
            pCtx->Eip = 0x43699c;
        }
    }) EHOOK_DY(th14_on_restart, 0x447810, 5, {
        Pointdevice::SelectRestartStage(pCtx);
        TH14InGameInfo::singleton().OnRestart();
        THAdvOptWnd::singleton().ToggleLock(true);
    })
    EHOOK_DY(th14_normal_game_enter, 0x45e63e, 8, {
        pointdeviceRun = pointdeviceEnabled && !pointdeviceLocked;
        pointdeviceLocked = true;
        THAdvOptWnd::singleton().ToggleLock(true);
    }) EHOOK_DY(th14_rep_save, 0x455bf2, 5, {
        char* repName = (char*)(pCtx->Esp + 0x38);
        if (thPracParam.mode == 1)
            THSaveReplay(repName);
        else if (thPracParam.mode == 2 && thPracParam.phase)
            THSaveReplay(repName);
        THAdvOptWnd::singleton().SaveReplay(mb_to_utf16(repName, 932).c_str());
    }) EHOOK_DY(th14_rep_menu_1, 0x45f10b, 3, {
        THGuiRep::singleton().State(1);
        THAdvOptWnd::singleton().ToggleLock(false);
        THAdvOptWnd::singleton().DeselectReplay();
    }) EHOOK_DY(th14_rep_menu_2, 0x45f216, 5, {
        THGuiRep::singleton().State(2);
        THAdvOptWnd::singleton().SelectReplay();
    }) EHOOK_DY(th14_rep_menu_3, 0x45f3ed, 2, {
        THGuiRep::singleton().State(3);
        THAdvOptWnd::singleton().ToggleLock(true);
    })
    EHOOK_DY(th14_sp_menu_1, 0x464068, 3, {
        if (THGuiSP::singleton().State()) {
            THAdvOptWnd::singleton().ToggleLock(true);
        } else {
            pCtx->Eip = 0x46409f;
        }
    })
    EHOOK_DY(th14_sp_menu_2, 0x463fd8, 6, {
        THAdvOptWnd::singleton().ToggleLock(false);
        if (THGuiSP::singleton().mState) {
            pCtx->Eip = 0x464068;
        }
    })
    EHOOK_DY(th14_pointdevice_before_update, 0x4012b0, 3, {
        if (Pointdevice::AdvanceRetry(GetTickCount64()))
            pCtx->Eip = 0x40135c; // Return success without running update callbacks.
    })
    EHOOK_DY(th14_pointdevice_keyboard, 0x401d0f, 7, {
        Pointdevice::FilterRetryKeyboard(pCtx, Pointdevice::retryVirtualKeys);
    })
    EHOOK_DY(th14_pointdevice_directinput, 0x401ee7, 7, {
        Pointdevice::FilterRetryKeyboard(pCtx, Pointdevice::retryScanCodes);
    })
    EHOOK_DY(th14_pointdevice_after_callback, 0x401313, 3, {
        // Miss and checkpoint restoration can start a freeze inside a callback.
        if (Pointdevice::RetryFreezesGameplay())
            pCtx->Eip = 0x40135c;
    })
    EHOOK_DY(th14_update, 0x40138a, 1, {
        GameGuiBegin(IMPL_WIN32_DX9, !THAdvOptWnd::singleton().IsOpen());

        // Gui components update
        THGuiPrac::singleton().Update();
        THGuiRep::singleton().Update();
        THOverlay::singleton().Update();
        THGuiSP::singleton().Update();
        TH14InGameInfo::singleton().Update();

        auto p = ImGui::GetOverlayDrawList();

        RenderBtHitbox(p);
        // in case boss movedown do not disabled when playing normal games
        {
            if (THAdvOptWnd::singleton().forceBossMoveDown) {
                auto sz = ImGui::CalcTextSize(S(TH_BOSS_FORCE_MOVE_DOWN));
                p->AddRectFilled({ 240.0f, 0.0f }, { sz.x + 240.0f, sz.y }, 0xFFCCCCCC);
                p->AddText({ 240.0f, 0.0f }, 0xFFFF0000, S(TH_BOSS_FORCE_MOVE_DOWN));
            }
        }

        // hit bar

        if (g_adv_igi_options.th14_showDropBar)
            RenderHitBar(p);

        if (g_adv_igi_options.show_keyboard_monitor && GetMemContent(PLAYER_PTR))
            KeysHUD(14, { 1280.0f, 0.0f }, { 840.0f, 0.0f }, g_adv_igi_options.keyboard_style);

        RenderLockTimer(p);
        RenderDisabledLifeSlot(p);
        RenderChapterFinish(p);
        GameUpdateOuter(p, 14);

        bool drawCursor = THAdvOptWnd::StaticUpdate() || THGuiPrac::singleton().IsOpen() || THGuiSP::singleton().IsOpen();
        GameGuiEnd(drawCursor);
    })
    EHOOK_DY(th14_player_state, 0x44DBD0,1,
    {
        if (g_adv_igi_options.show_keyboard_monitor)
            RecordKey(14, *(DWORD*)(0x004D6A90));
    }) 
    EHOOK_DY(th14_render, 0x40149a, 1, {
        if (GameGuiProgress == 2)
            RenderRetry(ImGui::GetOverlayDrawList());
        GameGuiRender(IMPL_WIN32_DX9);
    })
    EHOOK_DY(th14_retry_present, 0x46a68a, 3, {
        PresentRetryFrame(pCtx);
    })
    EHOOK_DY(th14_retry_present_alternate, 0x46a8e6, 3, {
        PresentRetryFrame(pCtx);
    })
    EHOOK_DY(th14_retry_present_fixed_rate, 0x46aafc, 3, {
        PresentRetryFrame(pCtx);
    })
    EHOOK_DY(th14_retry_present_clear, 0x46c322, 3, {
        PresentRetryFrame(pCtx, true);
    })
    EHOOK_DY(th14_retry_present_clear_second, 0x46c368, 3, {
        PresentRetryFrame(pCtx, true);
    })
    EHOOK_DY(th14_retry_release_backdrop, 0x447720, 6, {
        retryBackdrop.Reset();
    })
    HOOKSET_ENDDEF()

    HOOKSET_DEFINE(THInGameInfo)
    EHOOK_DY(th14_pointdevice_reward, 0x4397b0, 4, {
        if (pointdeviceRun) {
            const int itemType = GetMemContent<int>(pCtx->Esp + 4);
            if (itemType == lifeFragmentItem
                || (itemType == cycleRewardItem && GetMemContent<int>(BONUS_TOTAL) % 5 == 4))
                pCtx->Eip = reinterpret_cast<uintptr_t>(SpawnBombFragmentPair);
        }
    })
    EHOOK_DY(th14_game_start, 0x4375BE,7, // gamestart-bomb set
    {
        TH14InGameInfo::singleton().OnGameStart();
    })
    EHOOK_DY(th14_bomb_dec, 0x41218A,5, // bomb dec
    {
        ++chapterCounts.bombsUsed;
    })
    EHOOK_DY(th14_life_dec, 0x44F618,5, // life dec
    {
        TH14InGameInfo::singleton().mMissCount++;
        if (!Pointdevice::RequestRetry(pCtx))
            FastRetry(thPracParam.mode, pointdeviceRun && (thPracParam.mode == 1 || IsSpellPractice()));
    })
    EHOOK_DY(th14_get_item, 0x438DF8,6,
    {
        int item_cnt = *(int32_t*)(0x4F5880);
        if (item_cnt >= 60)
            ++chapterCounts.bonus20;
        else if (item_cnt >= 50)
            ++chapterCounts.bonus16;
        else if (item_cnt >= 40)
            ++chapterCounts.bonus12;
        else if (item_cnt >= 30)
            ++chapterCounts.bonus08;
        else if (item_cnt >= 20)
            ++chapterCounts.bonus05;
    })
    EHOOK_DY(th14_lock_timer1, 0x42EC6B,10, // initialize
    {
        g_lock_timer = 0;
    })
    EHOOK_DY(th14_lock_timer2, 0x4286BA,4, // SetNextPattern case 514
    {
        g_lock_timer = 0;
    })
    EHOOK_DY(th14_lock_timer3, 0x427888,2, // set boss mode case 512
    {
        g_lock_timer = 0;
    })
    EHOOK_DY(th14_lock_timer4, 0x42C0D4,6, // decrease time (update)
    {
        g_lock_timer_flag = true;
    })
    HOOKSET_ENDDEF() 
    static __declspec(noinline) void THGuiCreate()
    {
        if (ImGui::GetCurrentContext()) {
            return;
        }
        // Init
        GameGuiInit(IMPL_WIN32_DX9, 0x4d8f68, WINDOW_PTR,
            Gui::INGAGME_INPUT_GEN2, 0x4d6884, 0x4d6880, 0,
            (*((int32_t*)0x4f7a54) >> 2) & 0xf);
        // Keep TH14 menu shortcuts off graphics wrappers' GetAsyncKeyState hooks.
        Gui::KeyboardInputUseWindowMessages();
        const auto windowThread = GetWindowThreadProcessId(GetMemContent<HWND>(WINDOW_PTR), nullptr);
        if (windowThread)
            pointdeviceKeyHookInstalled = SetWindowsHookExW(WH_GETMESSAGE, PointdeviceKeyHook, nullptr, windowThread) != nullptr;

        SetDpadHook(0x401A8E, 3);
        // Gui components creation
        THGuiPrac::singleton();
        THGuiRep::singleton();
        THOverlay::singleton();
        THGuiSP::singleton();
        TH14InGameInfo::singleton();

        // Hooks
        EnableAllHooks(THMainHook);
        EnableAllHooks(THInGameInfo);

        // Reset thPracParam
        thPracParam.Reset();
    }
    HOOKSET_DEFINE(THInitHook)
    PATCH_DY(th14_disable_demo, 0x45996c, "ffffff7f")
    PATCH_DY(th14_disable_mutex, 0x469387, "90e9")
    PATCH_DY(th14_startup_1, 0x4595a5, "eb")
    PATCH_DY(th14_startup_2, 0x45a173, "eb")
    EHOOK_DY(th14_gui_init_1, 0x45a591, 3, {
        self->Disable();
        THGuiCreate();
    })
    EHOOK_DY(th14_gui_init_2, 0x46b37b, 1, {
        self->Disable();
        THGuiCreate();
    })
    HOOKSET_ENDDEF()
}

void TH14Init()
{
    EnableAllHooks(TH14::THInitHook);
    InitHook(14,(void*)0x46b4b3, (void*)0x46b2de);
}
}
