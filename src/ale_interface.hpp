#ifndef ALE_INTERFACE_H
#define ALE_INTERFACE_H

#include "emucore/FSNode.hxx"
#include "emucore/OSystem.hxx"
#include "os_dependent/SettingsWin32.hxx"
#include "os_dependent/OSystemWin32.hxx"
#include "os_dependent/SettingsUNIX.hxx"
#include "os_dependent/OSystemUNIX.hxx"
#include "common/Defaults.hpp"
#include "controllers/internal_controller.hpp"

// @todo 
static const std::string Version = "0.4";

// Display welcome message 
static std::string welcomeMessage() {
    // ALE welcome message
    std::ostringstream oss;
    oss << "A.L.E: Arcade Learning Environment (version "
        << Version << ")\n" 
        << "[Powered by Stella]\n"
        << "Use -help for help screen.";
    return oss.str();
}

static void disableBufferedIO() {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    cin.rdbuf()->pubsetbuf(0,0);
    cout.rdbuf()->pubsetbuf(0,0);
    cin.sync_with_stdio();
    cout.sync_with_stdio();
}

/**
   This class interfaces ALE with external code for controlling agents.
 */
class ALEInterface
{
public:
    std::streambuf * redirected_buffer;
    std::ofstream * os;
    std::string redirected_file;

    std::auto_ptr<OSystem> theOSystem;
#ifdef WIN32
    std::auto_ptr<SettingsWin32> theSettings;
#else
    std::auto_ptr<SettingsUNIX> theSettings;
#endif

    std::auto_ptr<ALEController> controller;

protected:
    reward_t episode_score; // Score accumulated throughout the course of an episode
    bool display_active;    // Should the screen be displayed or not
    int max_num_frames;     // Maximum number of frames for each episode

public:
    ALEInterface(bool display_screen=false): episode_score(0), display_active(display_screen) {
#ifndef __USE_SDL
        if (display_active) {
            cout << "Screen display requires directive __USE_SDL to be defined." << endl;
            cout << "Please recompile this code with flag '-D__USE_SDL'." << endl;
            cout << "Also ensure ALE has been compiled with USE_SDL active (see ALE makefile)." << endl;
            exit(0);
        }
#endif
        disableBufferedIO();
        std::cerr << welcomeMessage() << endl;
    }

    ~ALEInterface() {}

    // Loads and initializes a game. After this call the game should be ready to play.
    void loadROM(string rom_file) {
        int argc = 6;
        char** argv = new char*[argc];
        for (int i=0; i<=argc; i++) {
            argv[i] = new char[200];
        }
        strcpy(argv[0],"./ale");
        strcpy(argv[1],"-player_agent");
        strcpy(argv[2],"random_agent");
        strcpy(argv[3],"-display_screen");
        if (display_active) strcpy(argv[4],"true");
        else                strcpy(argv[4],"false");
        strcpy(argv[5],rom_file.c_str());  

        createOSystem(argc, argv);
        controller.reset(new InternalController(theOSystem.get()));
        max_num_frames = theOSystem->settings().getInt("max_num_frames_per_episode");
        reset_game();
    }

    // Resets the game
    void reset_game() {
        controller->m_environment.reset();
    }

    // Indicates if the game has ended
    bool game_over() {
        return (controller->m_environment.isTerminal() || 
                (max_num_frames > 0 && getEpisodeFrameNumber() >= max_num_frames));
    }

    // Applies an action to the game and returns the reward. It is the user's responsibility
    // to check if the game has ended and reset when necessary - this method will keep pressing
    // buttons on the game over screen.
    reward_t act(Action action) {
        controller->applyActions(action, PLAYER_B_NOOP);
        reward_t reward = controller->m_settings->getReward();
        controller->display();
        return reward;
    }

    // Returns the vector of legal actions. This should be called only after the rom is loaded.
    ActionVect getLegalActionSet() {
        return controller->m_settings->getAllActions();
    }

    // Returns the vector of the minimal set of actions needed to play the game.
    ActionVect getMinimalActionSet() {
        return controller->m_settings->getMinimalActionSet();
    }

    // Returns the frame number since the loading of the ROM
    int getFrameNumber() {
        return controller->m_environment.getFrameNumber();
    }

    // Returns the frame number since the start of the current episode
    int getEpisodeFrameNumber() {
        return controller->m_environment.getEpisodeFrameNumber();
    }

    void setMaxNumFrames(int newMax) {
        max_num_frames = newMax;
    }

    // Returns the current game screen
    const ALEScreen &getScreen() {
        return controller->m_environment.getScreen();
    }

    // Returns the current RAM content
    const ALERAM &getRAM() {
        return controller->m_environment.getRAM();
    }

protected:
    void redirectOutput(string & outputFile) {
        cerr << "Redirecting ... " << outputFile << endl;

        redirected_file = outputFile;

        os = new std::ofstream(outputFile.c_str(), ios_base::out | ios_base::app);
        redirected_buffer = std::cout.rdbuf(os->rdbuf());
    }

    void createOSystem(int argc, char* argv[]) {
#ifdef WIN32
        theOSystem.reset(new OSystemWin32());
        theSettings.reset(new SettingsWin32(theOSystem.get()));
#else
        theOSystem.reset(new OSystemUNIX());
        theSettings.reset(new SettingsUNIX(theOSystem.get()));
#endif
   
        setDefaultSettings(theOSystem->settings());

        theOSystem->settings().loadConfig();

        // process commandline arguments, which over-ride all possible config file settings
        string romfile = theOSystem->settings().loadCommandLine(argc, argv);

        // Load the configuration from a config file (passed on the command
        //  line), if provided
        string configFile = theOSystem->settings().getString("config", false);
   
        if (!configFile.empty())
            theOSystem->settings().loadConfig(configFile.c_str());

        theOSystem->settings().validate();
        theOSystem->create();
  
        string outputFile = theOSystem->settings().getString("output_file", false);
        if (!outputFile.empty())
            redirectOutput(outputFile);
   
        // attempt to load the ROM
        if (argc == 1 || romfile == "" || !FilesystemNode::fileExists(romfile)) {
		
            std::cerr << "No ROM File specified or the ROM file was not found." << std::endl;
            exit(1); 

        } else if (theOSystem->createConsole(romfile))  {
        
            std::cerr << "Running ROM file..." << std::endl;
            theOSystem->settings().setString("rom_file", romfile);

        } else {
            exit(1);
        }

        // seed random number generator
        if (theOSystem->settings().getString("random_seed") == "time") {
            cerr << "Random Seed: Time" << endl;
            srand((unsigned)time(0));
            //srand48((unsigned)time(0));
        } else {
            int seed = theOSystem->settings().getInt("random_seed");
            assert(seed >= 0);
            cerr << "Random Seed: " << seed << endl;
            srand((unsigned)seed);
            //srand48((unsigned)seed);
        }

        theOSystem->console().setPalette("standard");
    }
};

#endif
