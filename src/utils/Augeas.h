/*---------------------------------------------------------------------------*\
                          ____  _ _ __ _ __  ___ _ _
                         |_ / || | '_ \ '_ \/ -_) '_|
                         /__|\_, | .__/ .__/\___|_|
                             |__/|_|  |_|
\*---------------------------------------------------------------------------*/

#ifndef ZYPPER_UTIL_AUGEAS_H_
#define ZYPPER_UTIL_AUGEAS_H_

#include <string>

extern "C"
{
  #include <augeas.h>
}

#include "zypp/base/NonCopyable.h"

/**
 * Zypper's wrapper around Augeas.
 */
class Augeas : private zypp::base::NonCopyable
{
public:
  Augeas();
  ~Augeas();

  std::string get(const std::string & augpath) const;

  std::string getOption(const std::string & option) const;
  bool isCommented(const std::string & option, bool global) const;
  void comment(const std::string & option);
  void uncomment(const std::string & option);

  ::augeas * getAugPtr()
  { return _augeas; }

private:
  bool isCommented(const std::string & section, const std::string & option,
      bool global) const;

private:
  ::augeas * _augeas;
  std::string _homedir;
  bool _got_global_zypper_conf;
  bool _got_user_zypper_conf;

  mutable bool _last_get_result;
};

#endif /* ZYPPER_UTIL_AUGEAS_H_ */
