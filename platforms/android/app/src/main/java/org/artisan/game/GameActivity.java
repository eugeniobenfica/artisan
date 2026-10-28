package org.artisan.game;

import org.libsdl.app.SDLActivity;

// SDL's activity, loading the player (SDL is linked into it: one library).
public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"main"};
    }
}
