#include "SpotifyCommand.hpp"

#include "../../CommandManager.hpp"
#include <thread>
#include "Utils/Concurrency/TaskRuntime.hpp"

void SpotifyCommand::execute(const std::vector<std::string>& args) {
    if (!spotify.SetupSuccess()) {
        addCommandMessage("Spotify Credentials are not setup");
        return;
    }

    TaskRuntime::scheduleDetached([this, args]() {
        std::string msg;

        if (args.empty()) {
            addCommandMessage("Invalid Command. (Use .spotify help to get a list of commands!)");
            return;
        }

        const std::string command = String::toLower(args[0]);

        if (command == "resume") {
            spotify.play();
            msg = "Started Playing.";
        } else if (command == "pause" || command == "stop") {
            spotify.pause();
            msg = "Stopped Playing.";
        } else if (command == "next" || command == "skip") {
            spotify.next_track();
            msg = "Skipped to next track.";
        } else if (command == "previous" || command == "back") {
            spotify.previous_track();
            msg = "Playing the previous track.";
        } else if (command == "volume") {
            if (args.size() < 2) {
                addCommandMessage("Please provide a volume value.");
                return;
            }
            spotify.set_volume(args[1]);
            msg = "Volume set to " + args[1] + "%";
        } else if (command == "play") {
            if (args.size() <= 2) {
                addCommandMessage("Please provide the name of the song you want to play.");
                return;
            }

            std::ostringstream oss;
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) {
                    oss << " ";
                }
                oss << args[i];
            }

            std::string output;
            for (char c : (oss.str() + "     ")) {
                output += (c == ' ') ? "%20" : std::string(1, c);
            }
            spotify.play_song_by_name(output);

            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::string songName = spotify.get_song_name();
            msg = songName.empty()
                ? "Started playing, but could not retrieve song info."
                : "Started playing " + songName;
        } else if (command == "name") {
            msg = "Playing " + spotify.get_song_name();
        } else {
            msg = "Invalid Command. (Use .spotify help to get a list of commands!)";
        }

        addCommandMessage(msg);
    }, "spotify-command");
}
