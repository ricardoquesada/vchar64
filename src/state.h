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

    void setCharColor(int charIndex, int bitIndex, int colorIndex);
    int getCharColor(int charIndex, int bitIndex) const;

    void resetCharsBuffer();
    char* getCharsBuffer();

    int getColorAtIndex(int index) const {
        Q_ASSERT(index >=0 && index < 4);
        return _colors[index];
    }

    void setColorAtIndex(int index, int color) {
        Q_ASSERT(index >=0 && index < 4);
        Q_ASSERT(color >=0 && color < 16);
        _colors[index] = color;
    }

    int getCurrentColor() const {
        return _colors[_selectedColorIndex];
    }

    void setSelectedColorIndex(int index) {
        Q_ASSERT(index>=0 && index<4);
        _selectedColorIndex = index;
    }

    int getSelectedColorIndex() const {
        return _selectedColorIndex;
    }

    void setMultiColor(bool enabled) {
        _multiColor = enabled;
    }

    bool isMultiColor() const {
        return _multiColor;
    }

protected:
    State();
    ~State();

    int _charIndex;
    int _totalChars;

    char _chars[State::CHAR_BUFFER_SIZE];

    bool _multiColor;

    int _selectedColorIndex;
    int _colors[4];
};

#endif // STATE_H
