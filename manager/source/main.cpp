/*
 * Copyright (c) 2026 NedcloarBR
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <main_application.hpp>
#include <art.hpp>
#include <ui/animated_image.hpp>
#include <ui/theme.hpp>

namespace {

    AppletHookCookie g_focus_hook;

    /* HOME came to the front (the Manager lost focus): games a badge change took off HOME come back now, so HOME
     * fetches their new icons while it is the one on screen. */
    void OnAppletHook(AppletHookType hook, void *) {
        if (hook == AppletHookType_OnFocusState) {
            const bool in_focus = appletGetFocusState() == AppletFocusState_InFocus;
            ui::AnimatedImage::SetInFocus(in_focus);
            if (!in_focus) {
                usbl::FinishHomeRefresh();
            }
        }
    }

}

extern "C" void userAppInit() {
    romfsInit();
    nsInitialize();
}

extern "C" void userAppExit() {
    /* Games a badge change took off HOME come back once it is in front again. */
    usbl::FinishHomeRefresh();
    usbl::Exit();
    nsExit();
    romfsExit();
}

int main() {
    auto renderer_opts = pu::ui::render::RendererInitOptions(SDL_INIT_EVERYTHING, pu::ui::render::RendererHardwareFlags);
    renderer_opts.UseImage(pu::ui::render::ImgAllFlags);
    renderer_opts.SetPlServiceType(PlServiceType_User);
    renderer_opts.AddDefaultAllSharedFonts();
    for (const u32 size : ui::FontSizes) {
        renderer_opts.AddExtraDefaultFontSize(ui::S(size));
    }
    renderer_opts.SetInputPlayerCount(1);
    renderer_opts.AddInputNpadStyleTag(HidNpadStyleSet_NpadStandard);
    renderer_opts.AddInputNpadIdType(HidNpadIdType_Handheld);
    renderer_opts.AddInputNpadIdType(HidNpadIdType_No1);

    appletHook(&g_focus_hook, OnAppletHook, nullptr);

    auto app = ui::MainApplication::New(pu::ui::render::Renderer::New(renderer_opts));
    if (const Result rc = app->Load(); R_FAILED(rc)) {
        diagAbortWithResult(rc);
    }
    app->ShowWithFadeIn();
    art::Shutdown();
    ui::theme::Release();
    appletUnhook(&g_focus_hook);
    return 0;
}
