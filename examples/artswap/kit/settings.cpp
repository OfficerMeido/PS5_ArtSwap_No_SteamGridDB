/*
 * Art Swap - the full page: Settings.
 *
 * It follows the kit's Settings design, as page.hpp draws it.
 *
 * Copyright (C) 2026 Gary Riches
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "app.hpp"
#include "page.hpp"

#include "audio/cues.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <cmath>

namespace artswap
{

namespace
{

namespace gfx = hui::gfx;
namespace ui = hui::ui;
namespace tween = hui::tween;
using gfx::Color;
using gfx::Rect;
using hui::Action;
using hui::Direction;
using hui::audio::Cue;
using namespace page;

constexpr int kCategories = 2;
constexpr const char *kCategoryNames[kCategories] = {"Games", "About"};
} // namespace

// ================================================================ settings

int App::settings_rows(int category) const
{
    return category == 0 ? 2 : 1;
}

Rect App::settings_rail_rect(int category) const
{
    return {kMargin, kRailY + static_cast<float>(category) * kRailPitch, kRailW, kRailH};
}

Rect App::settings_row_rect(int category, int row) const
{
    // under the page's tiles: one row of them, or two on About
    const float tiles = category == 1 ? 2 * 128.0f + 16.0f : 140.0f;
    const float top = kPanel.y + 100.0f + tiles + 24.0f;
    return {kPanel.x + kPanelPad, top + static_cast<float>(row) * kRowPitch, kPanel.w - 2 * kPanelPad, kRowH};
}

void App::open_settings()
{
    settings_back_ = screen_;
    screen_ = Screen::settings;
    settings_in_rows_ = false;
    page_age_ = 0.0f;
    age_ = 0.0f;
    rail_marker_.snap(settings_rail_rect(settings_category_).y);
    page_focus_.snap(settings_rail_rect(settings_category_));
}

void App::update_settings(const hui::InputFrame &input, float dt, ui::Feedback &feedback)
{
    const int rows = settings_rows(settings_category_);
    auto change_category = [&](int next) {
        next = std::clamp(next, 0, kCategories - 1);
        if (next == settings_category_)
        {
            feedback.play(Cue::error, 1.0f, 0.0f, 0.5f);
            return;
        }
        settings_category_ = next;
        page_age_ = 0.0f;
        feedback.play(Cue::tab, 1.08f - 0.06f * static_cast<float>(next));
    };
    if (input.is_pressed(Action::page_prev))
        change_category(settings_category_ - 1);
    else if (input.is_pressed(Action::page_next))
        change_category(settings_category_ + 1);
    else if (!settings_in_rows_)
    {
        if (input.nav == Direction::up || input.nav == Direction::down)
            change_category(settings_category_ + (input.nav == Direction::down ? 1 : -1));
        else if ((input.nav == Direction::right || input.is_pressed(Action::confirm)) && rows > 0)
        {
            settings_in_rows_ = true;
            settings_row_[settings_category_] = std::clamp(settings_row_[settings_category_], 0, rows - 1);
            feedback.play(Cue::focus, 1.0f, 0.3f);
        }
        else if (input.is_pressed(Action::back))
        {
            feedback.play(Cue::back);
            screen_ = settings_back_;
            age_ = 0.3f;
            return;
        }
    }
    else
    {
        int &row = settings_row_[settings_category_];
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = std::clamp(row + (input.nav == Direction::down ? 1 : -1), 0, rows - 1);
            if (next != row)
            {
                row = next;
                feedback.play(Cue::focus, 1.0f, 0.3f);
            }
            else if (!input.nav_repeat)
                feedback.play(Cue::error, 1.0f, 0.0f, 0.5f);
        }
        else if (input.nav == Direction::left || input.is_pressed(Action::back))
        {
            settings_in_rows_ = false;
            feedback.play(Cue::back);
        }
        else if (input.is_pressed(Action::confirm))
            settings_select(feedback);
    }
    rail_marker_.target = settings_rail_rect(settings_category_).y;
    rail_marker_.update(dt, 20.0f);
    page_focus_.target(settings_in_rows_ ? settings_row_rect(settings_category_, settings_row_[settings_category_])
                                         : settings_rail_rect(settings_category_));
    page_focus_.update(dt, 22.0f);
}

void App::settings_select(ui::Feedback &feedback)
{
    const int row = settings_row_[settings_category_];
    if (settings_category_ == 0 && row == 0)
    {
        feedback.play(Cue::select);
        load_titles(true);
        toast("Refreshing the game list", "");
    }
    else if (settings_category_ == 0)
    {
        // PlayStation Store logos: asked of Sony only once the person has turned them on
        feedback.play(Cue::toggle);
        const bool on = !store_logos_;
        helper_.ask({"STORE", on ? "on" : "off"}, [this, on](Reply &reply) {
            if (!reply.ok)
            {
                toast("The setting was not changed", reply.error, true);
                return;
            }
            store_logos_ = on;
            store_unreachable_ = false; // asked again, the next time
        }, true);
    }
    else
    {
        feedback.play(Cue::modal_open);
        sheet_ = Sheet::log;
        log_text_ = "Reading the helper's log\xE2\x80\xA6";
        helper_.ask({"LOG"}, [this](Reply &reply) { log_text_ = reply.ok ? reply.text : reply.error; }, true);
    }
}

void App::draw_settings(gfx::DrawList &list) const
{
    const float in = tween::stagger(age_, 0, 0.06f, 0.5f);
    draw_header(list, fonts_, "Settings", in);

    // the rail: the plate of the active category, the focus, then the names on top
    const float rail_in = tween::stagger(age_, 1, 0.05f, 0.45f);
    list.push_opacity(rail_in);
    list.rounded_rect({kMargin, rail_marker_.value, kRailW, kRailH}, kRadius, kText.with_alpha(0.06f));
    list.circle(kMargin + 6, 866, 4, kAccent);
    ui::paragraph(list, fonts_.regular, "Pictures of your own, put on any game's home-screen art.", kMargin + 24, 874,
                  22, kRailW - 40, 32, kText.with_alpha(0.55f), 2);
    list.pop_opacity();

    const float panel_in = tween::stagger(age_, 3, 0.06f, 0.55f);
    list.push_opacity(panel_in);
    draw_panel(list, kPanel);
    list.pop_opacity();

    list.push_opacity(tween::stagger(age_, 4, 0.06f, 0.4f));
    draw_focus(list, page_focus_.value(), ui::breathe(clock_));
    list.pop_opacity();

    static const Glyph glyphs[kCategories] = {Glyph::games, Glyph::about};
    for (int i = 0; i < kCategories; ++i)
    {
        const float item_in = tween::stagger(age_, 1 + i, 0.05f, 0.45f);
        const Rect r = settings_rail_rect(i);
        const bool active = i == settings_category_;
        const float x = r.x - 28.0f * (1.0f - item_in);
        list.push_opacity(item_in);
        draw_rail_icon(list, glyphs[i], x + 46, r.cy(), active ? kAccent : kText.with_alpha(0.6f));
        ui::text(list, active ? fonts_.semibold : fonts_.regular, kCategoryNames[i], x + 88, centred(r.cy(), 28), 28,
                 kText.with_alpha(active ? 1.0f : 0.68f));
        list.pop_opacity();
    }
    list.rounded_rect({kMargin + 8, rail_marker_.value + 20, 4, kRailH - 40}, 2, kAccent.with_alpha(rail_in));

    // the page
    const int c = settings_category_;
    const float rise = 18.0f;
    const float title_in = tween::stagger(page_age_, 0, 0.05f, 0.3f);
    list.push_opacity(title_in * panel_in);
    static const char *titles[kCategories] = {"Your games", "About Art Swap"};
    ui::text(list, fonts_.semibold, titles[c], kPanel.x + kPanelPad + kRowPad, kPanel.y + 66 + rise * (1.0f - title_in),
             36, kText);
    list.pop_opacity();

    const float tile_w = (kPanel.w - 2 * kPanelPad - 16.0f) * 0.5f;
    auto tile_rect = [&](int i, float h) {
        return Rect{kPanel.x + kPanelPad + static_cast<float>(i % 2) * (tile_w + 16.0f),
                    kPanel.y + 100.0f + static_cast<float>(i / 2) * (h + 16.0f), tile_w, h};
    };
    auto staggered = [&](int index, const auto &draw) {
        const float t = tween::stagger(page_age_, index, 0.05f, 0.32f);
        list.push_opacity(t * panel_in);
        list.push_transform(1.0f, 0, 0, 0, rise * (1.0f - t));
        draw();
        list.pop_transform();
        list.pop_opacity();
    };

    std::vector<RowInfo> rows;
    std::string description;
    const int kept = kept_count();
    if (c == 0)
    {
        staggered(1, [&] {
            draw_tile(list, fonts_, tile_rect(0, 140), "GAMES FOUND", std::to_string(titles_.size()),
                      "PS5, PS4 and homebrew titles with art to change");
        });
        staggered(2, [&] {
            draw_tile(list, fonts_, tile_rect(1, 140), "ART CHANGED", std::to_string(kept),
                      kept ? (kept == 1 ? "Game with new art" : "Games with new art") : "Nothing changed yet");
        });
        rows = {{"Refresh game list", std::to_string(titles_.size()) + (titles_.size() == 1 ? " game" : " games")},
                {"PlayStation Store logos", store_logos_ ? "On" : "Off"}};
        static const char *notes[] = {
            "Finds games, apps and homebrew installed since Art Swap opened.",
            "Store games' own logos, from Sony's catalog: no account or console details, but Sony sees your IP."};
        description = settings_in_rows_ ? notes[settings_row_[0]]
                                        : "The titles on this console that have home-screen art.";
    }
    else
    {
        const std::string backend = helper_.backend().empty() ? "starting" : helper_.backend();
        staggered(1, [&] { draw_tile(list, fonts_, tile_rect(0, 128), "APPLICATION", std::string("Art Swap ") + kVersion, "PPSA99318"); });
        staggered(2, [&] {
            draw_tile(list, fonts_, tile_rect(1, 128), "HELPER", std::string("artswapd ") + kHelperVersion,
                      "Network: " + backend);
        });
        staggered(3, [&] {
            draw_tile(list, fonts_, tile_rect(2, 128), "THE ART", "Yours", "Pictures you send, and Store logos");
        });
        staggered(4, [&] {
            draw_tile(list, fonts_, tile_rect(3, 128), "MADE WITH", "PS5 Vulkan Template",
                      "UI kit by BlackBearReloaded, GPL-3.0");
        });
        rows = {{"Helper log", "/data/artswap", true}};
        description = settings_in_rows_ ? "What Art Swap's helper did: the first thing to send with a problem report."
                                        : "Versions, and the helper's log.";
    }
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        staggered(3 + i, [&] {
            draw_row(list, fonts_, settings_row_rect(c, i), rows[static_cast<std::size_t>(i)],
                     settings_in_rows_ && i == settings_row_[c]);
        });
    // under PlayStation Store logos: when it cannot work
    if (c == 0)
        staggered(5, [&] {
            const Rect row = settings_row_rect(0, 1);
            const float x = row.x + kRowPad, y = row.y + row.h + 44;
            list.circle(x + 5, y - 8, 4, kWarn);
            ui::paragraph(list, fonts_.regular,
                          "Most jailbroken consoles are kept from Sony's servers, to stop updates. If yours is, "
                          "PlayStation Store cannot be reached and this does nothing: the logo tab says so once.",
                          x + 22, y, 22, row.w - 2 * kRowPad - 22, 32, kText.with_alpha(0.6f), 3);
        });
    list.push_opacity(tween::stagger(page_age_, 6, 0.05f, 0.36f) * panel_in);
    draw_description(list, fonts_, kPanel, description);
    list.pop_opacity();
}

// ================================================================ the phone page

void App::close_phone()
{
    helper_.ask({"PHONE"}, nullptr, true);
}

// The picker opens with the person's own pictures first (pictures.cpp), and a store game's own logo after them
void App::want_picker(const std::string &slot, ui::Feedback &feedback)
{
    feedback.play(Cue::open);
    open_picker(focus_, slot);
}

} // namespace artswap
