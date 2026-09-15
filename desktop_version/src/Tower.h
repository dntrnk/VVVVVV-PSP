#ifndef TOWER_H
#define TOWER_H

class towerclass
{
public:
    towerclass(void);

    int backat(int xp, int yp, int yoff);

    int at(int xp, int yp, int yoff);

    int miniat(int xp, int yp, int yoff);

    void loadminitower1(void);

    void loadminitower2(void);

    void loadbackground(void);

    void loadmap(void);

    // Need to change to uint8_t later
    unsigned char back[40 * 120];
    unsigned char contents[40 * 700];
    unsigned char minitower[40 * 100];

    bool minitowermode;
};





#endif /* TOWER_H */
