#pragma once

class ILogAppender {
 public:
  virtual void Initialize() = 0;
  virtual void Log(const char* message) = 0;
};