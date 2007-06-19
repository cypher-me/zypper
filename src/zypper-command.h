#ifndef ZYPPERCOMMAND_H_
#define ZYPPERCOMMAND_H_

#include<iosfwd>
#include<string>

/**
 * Enumeration of <b>zypper</b> commands with mapping of command aliases.
 * The mapping includes <b>rug</b> equivalents as well.
 */
struct ZypperCommand
{
  static const ZypperCommand ADD_REPO;
  static const ZypperCommand REMOVE_REPO;
  static const ZypperCommand LIST_REPOS;
  static const ZypperCommand REFRESH;

  static const ZypperCommand INSTALL;
  static const ZypperCommand REMOVE;
  static const ZypperCommand UPDATE;
  
  static const ZypperCommand SEARCH;
  static const ZypperCommand INFO;
  static const ZypperCommand LIST_UPDATES;

  static const ZypperCommand HELP;
  static const ZypperCommand SHELL;
  static const ZypperCommand SHELL_QUIT;
  static const ZypperCommand MOO;

  /** Special void command value meaning <b>not set</b> */
  static const ZypperCommand NONE;

  /** name rug commands */
  //!@{
  static const ZypperCommand RUG_PATCH_INFO;
  //!@}

  enum Command
  {
    ADD_REPO_e,
    REMOVE_REPO_e,
    LIST_REPOS_e,
    REFRESH_e,

    INSTALL_e,
    REMOVE_e,
    UPDATE_e,
    
    SEARCH_e,
    INFO_e,
    LIST_UPDATES_e,
    
    HELP_e,
    SHELL_e,
    SHELL_QUIT_e,
    MOO_e,
    
    NONE_e,

    RUG_PATCH_INFO_e
  };

  ZypperCommand(Command command) : _command(command) {}

  explicit ZypperCommand(const std::string & strval_r);
  
  const Command toEnum() const { return _command; }

  ZypperCommand::Command parse(const std::string & strval_r);

  const std::string & asString() const;


  Command _command; 
};

inline std::ostream & operator<<( std::ostream & str, const ZypperCommand & obj )
{ return str << obj.asString(); }

inline bool operator==(const ZypperCommand & obj1, const ZypperCommand & obj2)
{ return obj1._command == obj2._command; }


#endif /*ZYPPERCOMMAND_H_*/
