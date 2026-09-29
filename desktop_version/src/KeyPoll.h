#ifndef KEYPOLL_H
#define KEYPOLL_H

#include <map> // FIXME: I should feel very bad for using C++ -flibit
#include <string>
#include <vector>
#include <cstdint>

#include "controls.h"

class KeyPoll
{
public:
    bool isActive;

    bool resetWindow;

    int sensitivity;

    int inline getThreshold(void);

    KeyPoll(void);

    void enabletextentry(void);

    void disabletextentry(void);

    void Poll(void);

    bool isDown(psp_key key);

    bool controllerWantsLeft(bool includeVert);
    bool controllerWantsRight(bool includeVert);
    bool controllerWantsUp(void);
    bool controllerWantsDown(void);
    bool stickWantsLeft(void);
    bool stickWantsRight(void);
    bool stickWantsUp(void);
    bool stickWantsDown(void);

    int leftbutton, rightbutton, middlebutton;
    int mousex;
    int mousey;

    bool textentry(void);
    bool pressedbackspace;
    std::string keybuffer;
    std::string imebuffer;
    int imebuffer_start;
    int imebuffer_length;

    bool osk_active;
    bool osk_done;
    bool osk_just_closed;
    std::string osk_result;
    uint16_t osk_intext[256];
    uint16_t osk_outtext[256];
    int osk_status = 0;

    bool linealreadyemptykludge;

private:
    int xVel, yVel;
    uint32_t wasFullscreen;
};

#ifndef KEY_DEFINITION
extern KeyPoll key;
#endif

#endif /* KEYPOLL_H */
