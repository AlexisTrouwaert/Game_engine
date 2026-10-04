#include "states_demo.hpp"

#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>

#include "moteur/assets.hpp"
#include "moteur/file_io.hpp"
#include "moteur/gpu_resource.hpp"
#include "moteur/image.hpp"
#include "moteur/process_memory.hpp"

#include <filesystem>

namespace {

// Autopilot, in ticks (60 per second): time on the title, in the game before each pause, and in
// each pause.
constexpr int kTitleTicks = 2;
constexpr int kFirstPauseAt = 20;
constexpr int kSecondPauseAt = 30;
constexpr int kPauseTicks = 5;

double megabytes(std::size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

// A window centred on the screen, without decoration.
bool begin_centered(const char* name) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    return ImGui::Begin(name, nullptr,
                        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoSavedSettings);
}

std::unique_ptr<moteur::GameState> make_title(StatesDemo& demo);
std::unique_ptr<moteur::GameState> make_loading(StatesDemo& demo, std::shared_ptr<const moteur::SaveGame> saved = nullptr);

constexpr int kThumbnailWidth = 320;  // pixels across
const char* const kManualSlots[] = {"emplacement_1", "emplacement_2", "emplacement_3"};

std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// "03/10/2026 14:22" in local time.
std::string local_time(std::int64_t seconds) {
    SDL_DateTime t{};
    if (!SDL_TimeToDateTime(static_cast<SDL_Time>(seconds) * SDL_NS_PER_SECOND, &t, true)) {
        return "?";
    }
    char text[32];
    std::snprintf(text, sizeof(text), "%02d/%02d/%04d %02d:%02d", t.day, t.month, t.year, t.hour, t.minute);
    return text;
}

std::string slot_label(const std::string& name) {
    if (name.starts_with("emplacement_")) {
        return "Emplacement " + name.substr(12);
    }
    if (name.starts_with("auto_")) {
        return "Sauvegarde automatique " + name.substr(5);
    }
    return name;
}

// A column of buttons driven by the menu actions (arrows, pad) or the mouse. The focused one is
// highlighted; the mouse does not move the focus (a replay would then depend on where it is).
class Menu {
public:
    explicit Menu(std::vector<std::string> items) : items_(std::move(items)) {}
    void set_items(std::vector<std::string> items) {
        items_ = std::move(items);
        focus_ = std::min(focus_, static_cast<int>(items_.size()) - 1);
    }
    const std::string& item(int index) const { return items_[static_cast<std::size_t>(index)]; }

    // The actions of this tick: the item confirmed, or -1.
    int update(StatesDemo& demo) {
        const moteur::Input& input = demo.app().input();
        const auto pressed = [&input](const char* name) {
            const moteur::ActionId action = input.find(name);
            return action != moteur::kNoAction && input.pressed(action);
        };
        const int count = static_cast<int>(items_.size());
        if (pressed("menu_up")) {
            focus_ = (focus_ + count - 1) % count;
            demo.play_click();
        }
        if (pressed("menu_down")) {
            focus_ = (focus_ + 1) % count;
            demo.play_click();
        }
        if (pressed("menu_confirm")) {
            demo.play_confirm();
            return focus_;
        }
        return -1;
    }

    // The buttons; the one clicked with the mouse, or -1.
    int draw(StatesDemo& demo) {
        int clicked = -1;
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            const bool focused = i == focus_;
            if (focused) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(items_[static_cast<std::size_t>(i)].c_str(), ImVec2(220.0f, 0.0f))) {
                clicked = i;
                demo.play_confirm();
            }
            if (focused) {
                ImGui::PopStyleColor();
            }
        }
        return clicked;
    }

    int focus() const { return focus_; }

private:
    std::vector<std::string> items_;
    int focus_ = 0;
};

// --- The game: the 3D demo, built by the loading screen's last step.
class GameplayState final : public moteur::GameState {
public:
    GameplayState(StatesDemo& demo, std::shared_ptr<const moteur::SaveGame> saved)
        : demo_(demo), world_(std::make_unique<Demo3D>(demo.app(), with_save(demo.options().demo, std::move(saved)), false)) {
        demo_.set_world(world_.get());
    }
    ~GameplayState() override {
        if (demo_.world() == world_.get()) {
            demo_.set_world(nullptr);
        }
    }

    const char* name() const override { return "game"; }
    Demo3D& world() { return *world_; }

    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override { world_->render(renderer, alpha); }

    // The pause must freeze the world: the demo counts its ticks, which must not move under it.
    void covered() override {
        covered_ = true;
        ticks_when_covered_ = world_->ticks();
    }
    void uncovered() override { check_frozen(); }
    void exit() override { check_frozen(); }  // left from the pause ("Retour au titre")
    void enter() override { demo_.app().audio().play_music(demo_.game_music()); }

private:
    static Demo3D::Options with_save(Demo3D::Options options, std::shared_ptr<const moteur::SaveGame> saved) {
        options.saved_game = std::move(saved);
        return options;
    }
    void check_frozen() {
        if (covered_) {
            demo_.check_frozen(world_->ticks() == ticks_when_covered_);
            covered_ = false;
        }
    }

    StatesDemo& demo_;
    std::unique_ptr<Demo3D> world_;
    long ticks_ = 0;  // ticks this state was updated (not paused)
    bool covered_ = false;
    long ticks_when_covered_ = 0;
};

// --- The pause: over the game, which stays visible and frozen.
class PauseState final : public moteur::GameState {
public:
    // leave: the autopilot goes back to the title from this pause (otherwise it resumes).
    PauseState(StatesDemo& demo, bool leave) : demo_(demo), leave_(leave) {}

    const char* name() const override { return "pause"; }
    bool transparent() const override { return true; }
    bool blocking() const override { return true; }

    // The rule chosen for the sound: the music goes on, lower; the world's sounds wait.
    void enter() override { set_paused(true); }
    void exit() override { set_paused(false); }

    void update(double) override {
        ++ticks_;
        moteur::Input& input = demo_.app().input();
        const moteur::ActionId pause = input.find("pause");
        if (pause != moteur::kNoAction && input.pressed(pause)) {
            resume();
        } else if (const int chosen = menu_.update(demo_); chosen >= 0) {
            choose(chosen);
        } else if (demo_.options().cycles > 0 && ticks_ == kPauseTicks) {
            if (leave_) {
                back_to_title();
            } else {
                resume();
            }
        }
    }

    void render(moteur::Renderer& renderer, double) override {
        if (!thumbnail_taken_) {
            // The first frame of the pause: the game as it is, without the menu, for the saves.
            renderer.request_capture(demo_.pause_thumbnail(), kThumbnailWidth);
            thumbnail_taken_ = true;
            return;
        }
        if (demo_.capture_pending() && ticks_ >= 2) {
            renderer.request_capture(demo_.options().capture_path);
            demo_.capture_done();
        }
        // The world, darkened, under the menu.
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
            IM_COL32(0, 0, 0, 120));
        if (begin_centered("Pause")) {
            ImGui::TextUnformatted("Pause");
            ImGui::TextDisabled("Le monde est figé dessous.");
            ImGui::Spacing();
            if (const int clicked = menu_.draw(demo_); clicked >= 0) {
                choose(clicked);
            }
            ImGui::TextDisabled("Échap ou Start : reprendre");
        }
        ImGui::End();
    }

private:
    void set_paused(bool paused) {
        moteur::Audio& audio = demo_.app().audio();
        audio.set_music_volume(paused ? 0.35f : 1.0f, 0.3f);
        audio.set_group_paused(moteur::SoundGroup::Effects, paused);
        audio.set_group_paused(moteur::SoundGroup::Ambience, paused);
    }
    void choose(int item) {
        if (item == 0) {
            resume();
        } else if (item == 1) {
            open_slots(true);
        } else {
            back_to_title();
        }
    }
    void open_slots(bool saving);
    void resume() {
        if (!stack()->has_pending()) {
            stack()->pop();
        }
    }
    void back_to_title() {
        if (!stack()->has_pending()) {
            stack()->reset(make_title(demo_));
        }
    }

    StatesDemo& demo_;
    bool leave_;
    int ticks_ = 0;
    bool thumbnail_taken_ = false;
    Menu menu_{{"Reprendre", "Sauvegarder", "Retour au titre"}};
};

// --- The slots: to save into (over the pause) or to load from (over the title).
class SlotsState final : public moteur::GameState {
public:
    SlotsState(StatesDemo& demo, bool saving) : demo_(demo), saving_(saving) {}

    const char* name() const override { return saving_ ? "sauvegarder" : "charger"; }
    bool transparent() const override { return saving_; }  // the frozen game under it
    bool blocking() const override { return true; }

    void enter() override { refresh(); }

    void update(double) override {
        moteur::Input& input = demo_.app().input();
        const moteur::ActionId pause = input.find("pause");
        if (pause != moteur::kNoAction && input.pressed(pause)) {
            back();
        } else if (const int chosen = menu_.update(demo_); chosen >= 0) {
            choose(chosen);
        }
    }

    void render(moteur::Renderer& renderer, double) override {
        if (!saving_) {
            renderer.set_clear_color(0.06f, 0.05f, 0.09f);
        }
        if (begin_centered(saving_ ? "Sauvegarder" : "Charger")) {
            ImGui::TextUnformatted(saving_ ? "Sauvegarder la partie" : "Charger une partie");
            ImGui::Spacing();
            for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
                const Row& row = rows_[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                const bool focused = i == menu_.focus();
                if (row.thumbnail.gpu) {
                    ImGui::Image(reinterpret_cast<ImTextureID>(row.thumbnail.gpu.get()), ImVec2(160.0f, 90.0f));
                } else {
                    ImGui::Dummy(ImVec2(160.0f, 90.0f));
                }
                ImGui::SameLine();
                ImGui::BeginGroup();
                if (ImGui::Selectable(slot_label(row.name).c_str(), focused, 0, ImVec2(320.0f, 0.0f))) {
                    demo_.play_confirm();
                    choose(i);
                }
                if (!row.exists) {
                    ImGui::TextDisabled("(vide)");
                } else if (!row.slot.readable) {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Illisible : %s", row.slot.error.c_str());
                } else {
                    const nlohmann::json& summary = row.slot.header.summary;
                    ImGui::TextDisabled("%s, %.0f min de jeu%s", local_time(row.slot.header.time).c_str(),
                                        row.slot.header.play_seconds / 60.0, row.slot.from_backup ? " (copie de secours)" : "");
                    ImGui::TextDisabled("Vie %d %%, %d créatures debout", static_cast<int>(summary.value("vie", 0.0) * 100.0),
                                        summary.value("creatures", 0));
                }
                ImGui::EndGroup();
                ImGui::PopID();
            }
            const bool back_focused = menu_.focus() == static_cast<int>(rows_.size());
            if (ImGui::Selectable("Retour", back_focused, 0, ImVec2(160.0f, 0.0f))) {
                back();
            }
            if (!status_.empty()) {
                ImGui::Spacing();
                ImGui::TextUnformatted(status_.c_str());
            }
            ImGui::TextDisabled("Flèches et Entrée ; Échap : retour");
        }
        ImGui::End();
    }

private:
    struct Row {
        std::string name;
        bool exists = false;
        moteur::SaveSlots::Slot slot;
        moteur::Texture thumbnail;
    };

    void refresh() {
        rows_.clear();
        const std::vector<moteur::SaveSlots::Slot> slots = demo_.slots().list();
        const auto make_row = [&](const std::string& name) {
            Row row;
            row.name = name;
            for (const moteur::SaveSlots::Slot& slot : slots) {
                if (slot.name == name) {
                    row.exists = true;
                    row.slot = slot;
                }
            }
            // Its thumbnail, if any (a bad file leaves the place empty).
            if (row.exists && !row.slot.thumbnail.empty()) {
                if (const auto bytes = moteur::read_file_bytes(row.slot.thumbnail)) {
                    try {
                        const moteur::Image image = moteur::decode_image(bytes->data(), bytes->size(), row.slot.thumbnail);
                        row.thumbnail = demo_.app().renderer().create_texture(image, "save thumbnail");
                    } catch (const std::exception& e) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Save: thumbnail %s: %s", row.slot.thumbnail.c_str(), e.what());
                    }
                }
            }
            rows_.push_back(std::move(row));
        };
        if (saving_) {
            for (const char* name : kManualSlots) {
                make_row(name);
            }
        } else {
            for (const moteur::SaveSlots::Slot& slot : slots) {  // every slot, the most recent first
                make_row(slot.name);
            }
        }
        std::vector<std::string> items;
        for (const Row& row : rows_) {
            items.push_back(row.name);
        }
        items.push_back("Retour");
        menu_.set_items(items);
    }

    void choose(int index) {
        if (index >= static_cast<int>(rows_.size())) {
            back();
            return;
        }
        const Row& row = rows_[static_cast<std::size_t>(index)];
        if (saving_) {
            std::string error;
            if (demo_.save_game(row.name, demo_.pause_thumbnail(), error)) {
                status_ = "Sauvegardé dans " + slot_label(row.name) + ".";
            } else {
                status_ = "Échec : " + error;
            }
            refresh();
        } else if (row.exists) {
            demo_.load_game(row.name);
        }
    }

    void back() {
        if (!stack()->has_pending()) {
            stack()->pop();
        }
    }

    StatesDemo& demo_;
    bool saving_;
    std::vector<Row> rows_;
    std::string status_;
    Menu menu_{{"Retour"}};
};

void PauseState::open_slots(bool saving) {
    if (!stack()->has_pending()) {
        stack()->push(std::make_unique<SlotsState>(demo_, saving));
    }
}

void GameplayState::update(double dt) {
    ++ticks_;
    if (world_->pause_pressed()) {
        stack()->push(std::make_unique<PauseState>(demo_, false));
    } else if (demo_.options().cycles > 0 && (ticks_ == kFirstPauseAt || ticks_ == kSecondPauseAt)) {
        stack()->push(std::make_unique<PauseState>(demo_, ticks_ == kSecondPauseAt));
    }
    world_->update(dt);
    demo_.played(dt);
}

// --- Loading: the demo's files first (kept until the world is built, which then finds them in the
// cache), then the world.
class DemoLoadingState final : public moteur::LoadingState {
public:
    DemoLoadingState(StatesDemo& demo, std::vector<Step> steps, MakeState make_next)
        : LoadingState(std::move(steps), "Construction du monde", std::move(make_next)), demo_(demo) {}

    void enter() override { demo_.loading_started(); }

protected:
    void draw(moteur::Renderer& renderer, float progress, const std::string& label) override {
        renderer.set_clear_color(0.02f, 0.02f, 0.03f);
        if (begin_centered("Chargement")) {
            ImGui::TextUnformatted("Chargement");
            ImGui::ProgressBar(progress, ImVec2(320.0f, 0.0f));
            ImGui::TextDisabled("%s", label.c_str());
        }
        ImGui::End();
    }

    void failed(const std::string& error) override {
        SDL_Log("States: loading failed: %s", error.c_str());
        demo_.set_error(error);
        stack()->reset(make_title(demo_));
    }

private:
    StatesDemo& demo_;
};

std::unique_ptr<moteur::GameState> make_loading(StatesDemo& demo, std::shared_ptr<const moteur::SaveGame> saved) {
    struct Held {
        moteur::Asset<moteur::Font> font;
        moteur::Asset<moteur::Model> barrel;
        std::vector<moteur::Asset<moteur::Model>> characters;
        std::vector<moteur::Asset<moteur::Skeleton>> skeletons;
        std::vector<moteur::Asset<moteur::ClipLibrary>> clips;
        moteur::Asset<moteur::AnimationSet> animation_set;
        std::vector<moteur::Asset<moteur::Sound>> sounds;
        moteur::Asset<moteur::Music> ambience;
    };
    auto held = std::make_shared<Held>();
    moteur::Assets& assets = demo.app().assets();
    std::vector<moteur::LoadingState::Step> steps = {
        {"Police", [&assets, held] { held->font = assets.font(Demo3D::kFont, Demo3D::kFontPixelHeight); }},
        {"Modèle du tonneau",
         [&assets, held] {
             if (assets.exists(Demo3D::kBarrel)) {
                 held->barrel = assets.model(Demo3D::kBarrel);
             }
         }},
        {"Personnages animés",
         [&assets, held] {
             if (!Demo3D::characters_available(assets)) {  // tools/models/fetch_test_characters.py
                 return;
             }
             for (const char* path : {Demo3D::kKnight, Demo3D::kSkeletons[0], Demo3D::kSkeletons[1]}) {
                 held->characters.push_back(assets.model(path));
                 held->skeletons.push_back(assets.skeleton(path));
                 held->clips.push_back(assets.clips(path));
             }
             held->characters.push_back(assets.model(Demo3D::kSword));
             held->animation_set = assets.animation_set(Demo3D::kAnimationSet);
         }},
        {"Sons",
         [&assets, held] {
             if (assets.exists(Demo3D::kAmbience)) {  // tools/audio/fetch_test_sounds.py
                 for (const char* path : Demo3D::kSteps) {
                     held->sounds.push_back(assets.sound(path));
                 }
                 for (const char* path : Demo3D::kImpacts) {
                     held->sounds.push_back(assets.sound(path));
                 }
                 held->ambience = assets.music(Demo3D::kAmbience);
             }
         }},
    };
    // `held` lives in the steps until the loading state goes, after the world was built.
    return std::make_unique<DemoLoadingState>(demo, std::move(steps), [&demo, saved] {
        auto game = std::make_unique<GameplayState>(demo, saved);
        demo.loading_done();
        return game;
    });
}

// --- The title screen.
class TitleState final : public moteur::GameState {
public:
    explicit TitleState(StatesDemo& demo) : demo_(demo) {}

    const char* name() const override { return "title"; }
    void enter() override {
        demo_.app().audio().play_music(demo_.title_music());
        std::vector<std::string> items = {"Jouer"};
        if (!demo_.latest_slot().empty()) {
            items.push_back("Continuer");
        }
        items.push_back("Charger");
        items.push_back("Quitter");
        menu_.set_items(items);
    }
    void uncovered() override { enter(); }  // back from "Charger"

    void update(double) override {
        if (ticks_++ == 0) {
            demo_.measure();  // the previous game is gone and its assets collected by now
        }
        if (demo_.options().cycles > 0) {
            if (demo_.cycles_done() >= demo_.options().cycles) {
                if (!finished_) {
                    finished_ = true;
                    demo_.finish();
                }
            } else if (ticks_ >= kTitleTicks) {
                play();
            }
        } else if (const int chosen = menu_.update(demo_); chosen >= 0) {
            choose(chosen);
        }
    }

    void render(moteur::Renderer& renderer, double) override {
        renderer.set_clear_color(0.06f, 0.05f, 0.09f);
        if (begin_centered("Écran titre")) {
            ImGui::TextUnformatted("Tranche jouable");
            ImGui::TextDisabled("Titre, chargement, la carte avec un héros, pause.");
            ImGui::TextDisabled("Flèches et Entrée, ou croix et A de la manette.");
            if (!demo_.error().empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Échec : %s", demo_.error().c_str());
            }
            ImGui::Spacing();
            if (const int clicked = menu_.draw(demo_); clicked >= 0) {
                choose(clicked);
            }
        }
        ImGui::End();
    }

private:
    void choose(int item) {
        const std::string& chosen = menu_.item(item);
        if (chosen == "Jouer") {
            play();
        } else if (chosen == "Continuer") {
            demo_.load_game(demo_.latest_slot());
        } else if (chosen == "Charger") {
            if (!stack()->has_pending()) {
                stack()->push(std::make_unique<SlotsState>(demo_, false));
            }
        } else {
            demo_.finish();
        }
    }
    void play() {
        if (!stack()->has_pending()) {
            demo_.set_error({});
            stack()->replace(make_loading(demo_));
        }
    }

    StatesDemo& demo_;
    int ticks_ = 0;
    bool finished_ = false;
    Menu menu_{{"Jouer", "Charger", "Quitter"}};
};

std::unique_ptr<moteur::GameState> make_title(StatesDemo& demo) {
    return std::make_unique<TitleState>(demo);
}

}  // namespace

StatesDemo::StatesDemo(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone), slots_(app.preferences_directory() + "sauvegardes/"), stack_(app) {
    moteur::Variables& variables = app.console().variables();
    // Declared again by every run of the scene: the same variables (and the player's values).
    save_binary_ = &variables.add_bool("save.binary", false, "sauvegardes en CBOR (celles du jeu livré) plutôt qu'en JSON",
                                       moteur::Variable::Archive);
    save_auto_ = &variables.add_int("save.auto", 120, 0, 3600, "secondes de jeu entre deux sauvegardes automatiques (0 : jamais)",
                                    moteur::Variable::Archive);
    app.console().add("save", "[emplacement]", "sauvegarde la partie en cours (emplacement_1 par défaut)", moteur::Console::Kind::Tool,
                      [this](const moteur::Console::Args& args, moteur::Console& out) {
                          std::string error;
                          const std::string slot = args.empty() ? "emplacement_1" : args[0];
                          if (save_game(slot, {}, error)) {
                              out.print("sauvegardé dans " + slots_.path(slot));
                          } else {
                              out.error(error);
                          }
                      });
    app.console().add("load", "[emplacement]", "charge une sauvegarde (la plus récente par défaut)", moteur::Console::Kind::Tool,
                      [this](const moteur::Console::Args& args, moteur::Console& out) {
                          const std::string slot = args.empty() ? latest_slot() : args[0];
                          if (slot.empty()) {
                              out.error("aucune sauvegarde lisible");
                              return;
                          }
                          out.print("chargement de " + slot);
                          load_game(slot);
                      },
                      [this](std::size_t index, const std::string& prefix) {
                          std::vector<std::string> names;
                          for (const moteur::SaveSlots::Slot& slot : slots_.list()) {
                              if (index == 0 && slot.name.starts_with(prefix)) {
                                  names.push_back(slot.name);
                              }
                          }
                          return names;
                      });
    options_.demo.hero = true;  // the game of the slice
    Demo3D::declare_actions(app);  // the menus' actions, before the demo exists
    if (app.assets().exists("audio/music/the_field_of_dreams.mp3")) {  // tools/audio/fetch_test_sounds.py
        title_music_ = app.assets().music("audio/music/the_field_of_dreams.mp3");
        game_music_ = app.assets().music("audio/music/town_theme.mp3");
        click_ = app.assets().sound("audio/kenney_interface/click_001.ogg");
        confirm_ = app.assets().sound("audio/kenney_interface/confirmation_001.ogg");
    }
    stack_.push(make_title(*this));
    stack_.apply_pending();
    if (moteur::DebugTools* tools = app.debug_tools()) {
        tools->watch(stack_, "Test des états");
    }
}

StatesDemo::~StatesDemo() {
    app_.console().remove("save");
    app_.console().remove("load");
    if (moteur::DebugTools* tools = app_.debug_tools()) {
        tools->forget(stack_);
    }
    app_.audio().stop_music(0.5f);
}

void StatesDemo::on_event(const SDL_Event& event) {
    stack_.on_event(event);
}

void StatesDemo::update(double dt) {
    elapsed_ += dt;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        finish();
    }
    stack_.update(dt);
}

void StatesDemo::render(moteur::Renderer& renderer, double alpha) {
    if (!pending_thumbnail_.empty()) {
        renderer.request_capture(pending_thumbnail_, kThumbnailWidth);  // this frame, as it is
        pending_thumbnail_.clear();
    }
    stack_.render(renderer, alpha);
}

bool StatesDemo::save_game(const std::string& slot, const std::string& thumbnail, std::string& error) {
    if (world_ == nullptr) {
        error = "aucune partie en cours";
        return false;
    }
    if (slot.empty() || slot.find_first_of("/\\:.") != std::string::npos) {
        error = "nom d'emplacement invalide : " + slot;
        return false;
    }
    const Uint64 start = SDL_GetPerformanceCounter();
    moteur::SaveGame game = world_->save();
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    game.header.game_version = "bac_a_sable (jalon 7)";
    game.header.time = static_cast<std::int64_t>(now / SDL_NS_PER_SECOND);
    game.header.play_seconds = world_->elapsed();
    game.header.summary = {{"lieu", "tranche"}, {"vie", world_->hero_health()}, {"creatures", world_->creatures_standing()},
                           {"tick", world_->ticks()}};
    const moteur::SaveEncoding encoding = save_binary_->as_bool() ? moteur::SaveEncoding::Cbor : moteur::SaveEncoding::Json;
    if (!slots_.write(slot, game, encoding, error)) {
        return false;
    }
    std::error_code copy_error;
    if (!thumbnail.empty() && std::filesystem::exists(utf8_path(thumbnail), copy_error)) {
        std::filesystem::copy_file(utf8_path(thumbnail), utf8_path(slots_.thumbnail_path(slot)),
                                   std::filesystem::copy_options::overwrite_existing, copy_error);
    } else {
        pending_thumbnail_ = slots_.thumbnail_path(slot);
    }
    SDL_Log("Save: '%s' written in %.1f ms (%s)", slot.c_str(),
            static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()),
            encoding == moteur::SaveEncoding::Cbor ? "CBOR" : "JSON");
    return true;
}

void StatesDemo::load_game(const std::string& slot) {
    std::string error;
    bool from_backup = false;
    std::optional<moteur::SaveGame> game = slots_.read(slot, error, nullptr, &from_backup);
    if (!game) {
        set_error("sauvegarde « " + slot_label(slot) + " » illisible : " + error);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Save: %s", error_.c_str());
        stack_.reset(make_title(*this));
        return;
    }
    if (from_backup) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Save: '%s' was damaged: its backup is loaded", slot.c_str());
    }
    set_error({});
    since_autosave_ = 0.0;
    stack_.reset(make_loading(*this, std::make_shared<const moteur::SaveGame>(std::move(*game))));
}

void StatesDemo::played(double dt) {
    const int every = save_auto_->as_int();
    if (every <= 0 || world_ == nullptr) {
        return;
    }
    since_autosave_ += dt;
    if (since_autosave_ < static_cast<double>(every)) {
        return;
    }
    since_autosave_ = 0.0;
    const std::string slot = next_auto_ == 0 ? "auto_1" : "auto_2";  // in turn: a damaged write keeps the other
    next_auto_ = 1 - next_auto_;
    std::string error;
    if (!save_game(slot, {}, error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Save: automatic save failed: %s", error.c_str());
    }
}

std::string StatesDemo::latest_slot() const {
    for (const moteur::SaveSlots::Slot& slot : slots_.list()) {  // the most recent first
        if (slot.readable) {
            return slot.name;
        }
    }
    return {};
}

bool StatesDemo::uses_escape() const {
    const moteur::GameState* top = stack_.top();
    return top != nullptr && (std::strcmp(top->name(), "game") == 0 || std::strcmp(top->name(), "pause") == 0 ||
                              std::strcmp(top->name(), "sauvegarder") == 0 || std::strcmp(top->name(), "charger") == 0);
}

void StatesDemo::measure() {
    Measure measure;
    for (const moteur::AssetTypeStats& type : app_.assets().stats()) {
        measure.assets += type.count;
        measure.asset_bytes += type.bytes;
    }
    measure.gpu_objects = moteur::gpu_resource_counts().total();
    measure.process_bytes = moteur::process_memory_bytes();
    measures_.push_back(measure);
    SDL_Log("States: cycle %d: %zu assets (%.1f MB), %ld GPU objects, process %.1f MB", cycles_done(),
            measure.assets, megabytes(measure.asset_bytes), measure.gpu_objects, megabytes(measure.process_bytes));
}

void StatesDemo::loading_started() {
    loading_start_ = SDL_GetPerformanceCounter();
}

void StatesDemo::loading_done() {
    last_load_ms_ = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - loading_start_) /
                    static_cast<double>(SDL_GetPerformanceFrequency());
    std::size_t asset_bytes = 0;
    for (const moteur::AssetTypeStats& type : app_.assets().stats()) {
        asset_bytes += type.bytes;
    }
    SDL_Log("Slice: loaded in %.0f ms; assets %.1f MB, %ld GPU objects, process %.1f MB", last_load_ms_,
            megabytes(asset_bytes), moteur::gpu_resource_counts().total(), megabytes(moteur::process_memory_bytes()));
}

void StatesDemo::play_click() {
    if (click_) {
        moteur::PlaySound options;
        options.group = moteur::SoundGroup::Interface;
        options.volume = 0.5f;
        app_.audio().play(click_, options);
    }
}

void StatesDemo::play_confirm() {
    if (confirm_) {
        moteur::PlaySound options;
        options.group = moteur::SoundGroup::Interface;
        options.volume = 0.6f;
        app_.audio().play(confirm_, options);
    }
}

void StatesDemo::finish() {
    if (standalone_) {
        app_.quit();
    } else {
        stop_requested_ = true;
    }
}

void StatesDemo::report() const {
    std::cout << "states: " << cycles_done() << " cycles, " << frozen_failures_ << " pauses where the world moved\n";
    if (measures_.size() < 2) {
        return;
    }
    // The first cycle loads what stays for good (pipelines, shared assets): compare from the end of it.
    const Measure& first = measures_[1];
    const Measure& last = measures_.back();
    std::cout << "states: after cycle 1: " << first.assets << " assets, " << first.gpu_objects << " GPU objects, "
              << megabytes(first.process_bytes) << " MB\n";
    std::cout << "states: after cycle " << cycles_done() << ": " << last.assets << " assets, " << last.gpu_objects
              << " GPU objects, " << megabytes(last.process_bytes) << " MB\n";
    const bool stable = first.assets == last.assets && first.asset_bytes == last.asset_bytes &&
                        first.gpu_objects == last.gpu_objects;
    std::cout << "states: assets and GPU objects " << (stable ? "stable" : "GROWING") << '\n';
    // The process's memory wanders by a few MB from one measure to the next (allocator, driver):
    // a leak shows as a floor that rises. The lowest of the first ten cycles against the last ten.
    const std::size_t count = measures_.size() - 1;
    const std::size_t window = std::min<std::size_t>(10, count);
    std::size_t low_start = SIZE_MAX, low_end = SIZE_MAX, high = 0;
    for (std::size_t i = 1; i <= count; ++i) {
        const std::size_t bytes = measures_[i].process_bytes;
        if (i <= window) {
            low_start = std::min(low_start, bytes);
        }
        if (i > count - window) {
            low_end = std::min(low_end, bytes);
        }
        high = std::max(high, bytes);
    }
    const double drift = megabytes(low_end) - megabytes(low_start);
    char line[200];
    std::snprintf(line, sizeof(line),
                  "states: process memory %.1f to %.1f MB; lowest of the first %zu cycles %.1f MB, of the last %zu %.1f MB (%+.1f MB)",
                  megabytes(std::min(low_start, low_end)), megabytes(high), window, megabytes(low_start), window,
                  megabytes(low_end), drift);
    std::cout << line << '\n';
}

void StatesDemo::draw_controls() {
    std::string states;
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        states += (i > 0 ? " > " : "") + std::string(stack_.at(i)->name());
    }
    ImGui::Text("Pile d'états : %s", states.c_str());
    ImGui::Text("Cycles : %d%s", cycles_done(), options_.cycles > 0 ? " (pilote automatique)" : "");
    if (!measures_.empty()) {
        const Measure& last = measures_.back();
        ImGui::Text("Au dernier titre : %zu assets (%.1f Mo GPU), %ld objets GPU, processus %.1f Mo", last.assets,
                    megabytes(last.asset_bytes), last.gpu_objects, megabytes(last.process_bytes));
    }
    if (last_load_ms_ > 0.0) {
        ImGui::Text("Dernier chargement : %.0f ms", last_load_ms_);
    }
    if (frozen_failures_ > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%d pauses où le monde a bougé", frozen_failures_);
    }
    if (auto* game = dynamic_cast<GameplayState*>(stack_.top()); game != nullptr) {
        if (ImGui::CollapsingHeader("Démo 3D")) {
            game->world().draw_controls();
        }
    }
}
