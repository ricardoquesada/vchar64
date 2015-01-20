#ifndef STATE_H
#define STATE_H

#include <QString>
#include <string>

class State
{
public:
    static State* getInstance();

    void loadCharSet(const std::string &filename);

    const char* getCharAtIndex(int index) {
        Q_ASSERT(index>=0 && index<256 && "Invalid index");
        return &_chars[index*8];
    }

protected:
    State();
    ~State();

    int _charIndex;
    int _totalChars;

    char _chars[8 * 256];
    bool _multiColor;

    int _color;
    int _multiColor0;
    int _multiColor1;
};

#endif // STATE_H
