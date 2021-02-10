#pragma once

class Logger
{
public:
    void Write(char *message);
    void WriteLine(char *message);

private:
    static const unsigned char MAX_BUFFER_SIZE = 100;
    char *_buffer[MAX_BUFFER_SIZE];
};
