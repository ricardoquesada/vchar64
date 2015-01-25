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

    int getColor(int index) const {
        Q_ASSERT(index >=0 && index < 4);
        return _colors[index];
    }

    void setColor(int index, int color) {
        Q_ASSERT(index >=0 && index < 4);
        Q_ASSERT(color >=0 && color < 16);
        _colors[index] = color;
    }

    int getSelectedColor() const {
        return _selectedColor;
    }

    void setSelectedColor(int index) {
        Q_ASSERT(index>=0 && index<4);
        _selectedColor = index;
    }

protected:
    State();
    ~State();

    int _charIndex;
    int _totalChars;

    char _chars[State::CHAR_BUFFER_SIZE];

    bool _multiColor;

    int _selectedColor;
    int _colors[4];
};

#endif // STATE_H
