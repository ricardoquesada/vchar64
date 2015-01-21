#ifndef STATE_H
#define STATE_H

#include <QString>
#include <string>

class State
{
public:
    // only 256 chars at the time
    const static int CHAR_BUFFER_SIZE = 8 * 256;

    static State* getInstance();

    bool loadCharSet(const QString &filename);

    const char* getCharAtIndex(int index) {
        Q_ASSERT(index>=0 && index<256 && "Invalid index");
        return &_chars[index*8];
    }

    void toggleBit(int charIndex, int bitIndex);
    void setBit(int charIndex, int bitIndex, bool enabled);
    bool getBit(int charIndex, int bitIndex) const;

    void resetCharsBuffer();
    char* getCharsBuffer();

protected:
    State();
    ~State();

    int _charIndex;
    int _totalChars;

    char _chars[State::CHAR_BUFFER_SIZE];
    bool _multiColor;

    int _color;
    int _multiColor0;
    int _multiColor1;
};

#endif // STATE_H
