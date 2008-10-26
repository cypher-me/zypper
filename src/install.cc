#include <boost/format.hpp>

#include "zypp/ZYppFactory.h"
#include "zypp/base/Logger.h"
#include "zypp/base/Algorithm.h"
#include "zypp/base/Functional.h"
#include "zypp/Filter.h"
#include "zypp/PoolQuery.h"

#include "utils/misc.h"

#include "install.h"
#include "update.h"

using namespace std;
using namespace zypp;
using namespace boost;

extern ZYpp::Ptr God;

/** Use ui::Selectable::theObj() or candidateObj() */
#define USE_THE_ONE 0

static PoolItem findInstalledItemInRepos(const PoolItem & installed)
{
  const zypp::ResPool & pool(zypp::ResPool::instance());
  PoolItem result;
  invokeOnEach(
    pool.byIdentBegin(installed), pool.byIdentEnd(installed),
    functor::chain(
      filter::SameItemAs(installed),
      resfilter::ByUninstalled()),
    functor::getFirst(result));
  INT << "findInstalledItemInRepos(" << installed << ") => " << result << endl;
  return result;
}

// TODO edition, arch ?
static void mark_for_install(Zypper & zypper,
                      const ResObject::Kind &kind,
                      const std::string &name,
                      const std::string & repo = "")
{
  // name and kind match:
  DBG << "Iterating over [" << kind << "]" << name << endl;

  PoolQuery q;
  q.addAttribute(sat::SolvAttr::name, name);
  if (!repo.empty())
    q.addRepo(repo);
  q.setCaseSensitive(false);
  q.setMatchExact();

  DBG << "... done" << endl;

  if (q.empty())
  {
    zypper.out().error(
      // translators: meaning a package %s or provider of capability %s
      str::form(_("'%s' not found"), name.c_str()));
    WAR << str::form("'%s' not found", name.c_str()) << endl;
    zypper.setExitCode(ZYPPER_EXIT_INF_CAP_NOT_FOUND);
    if (zypper.globalOpts().non_interactive)
      ZYPP_THROW(ExitRequestException());
    return;
  }

  ui::Selectable::Ptr s = *q.selectableBegin();
  bool force = copts.count("force");

#if USE_THE_ONE
  PoolItem candidate = s->candidateObj();
#else
  PoolItem candidate = findUpdateItem(God->pool(), s->installedObj());
  if (!candidate)
  {
    if (force)
    {
      candidate = findInstalledItemInRepos(s->installedObj());
      if (!candidate)
      {
        zypper.out().info(str::form(
            // translators: %s-%s.%s is name-version.arch
            _("Package %s-%s.%s not found in repositories, cannot reinstall."),
            s->name().c_str(), s->installedObj().resolvable()->edition().c_str(),
            s->installedObj().resolvable()->arch().c_str()));
        zypper.setExitCode(ZYPPER_EXIT_ERR_ZYPP);
        return;
      }
    }
    else
      candidate = s->installedObj();
  }
#endif

  if (s->installedObj() &&
      s->installedObj().resolvable()->edition() == candidate.resolvable()->edition() &&
      s->installedObj().resolvable()->arch() == candidate.resolvable()->arch() &&
      !force)
  {
    // if it is broken install anyway, even if it is installed
    if (candidate.isBroken())
      candidate.status().setToBeInstalled(zypp::ResStatus::USER);
    else
      zypper.out().info(boost::str(format(
        // translators: e.g. skipping package 'zypper' (the newest version already installed)
        //! \todo capitalize the first letter
        _("skipping %s '%s' (the newest version already installed)"))
        % kind_to_string_localized(kind,1) % name));
  }
  else if (!candidate.status().setToBeInstalled(zypp::ResStatus::USER) && !force)
  {
      zypper.out().error(boost::str(
        format(_("Failed to add '%s' to the list of packages to be installed."))
        % name));
      ERR << "Could not set " << name << " as to-be-installed" << endl;
  }
}

// ----------------------------------------------------------------------------

struct DeleteProcess
{
  bool found;
  DeleteProcess ()
    : found(false)
  { }

  bool operator() ( const PoolItem& provider )
  {
    found = true;
    DBG << "Marking for deletion: " << provider << endl;
    bool result = provider.status().setToBeUninstalled( zypp::ResStatus::USER );
    if (!result) {
      Zypper::instance()->out().error(boost::str(format(
          _("Failed to add '%s' to the list of packages to be removed."))
          % provider.resolvable()->name()));
      ERR << "Could not set " << provider.resolvable()->name()
          << " as to-be-uninstalled" << endl;
    }
    return true;                // get all of them
  }
};

// ----------------------------------------------------------------------------

// mark all matches
static void mark_for_uninstall(Zypper & zypper,
                        const ResObject::Kind &kind,
                        const std::string &name)
{
  const ResPool &pool = God->pool();
  // name and kind match:

  DeleteProcess deleter;
  DBG << "Iterating over " << name << endl;
  invokeOnEach( pool.byIdentBegin( kind, name ),
                pool.byIdentEnd( kind, name ),
                resfilter::ByInstalled(),
                zypp::functor::functorRef<bool,const zypp::PoolItem&> (deleter)
                );
  DBG << "... done" << endl;
  if (!deleter.found) {
    zypper.out().error(
      // translators: meaning a package %s or provider of capability %s
      str::form(_("'%s' not found"), name.c_str()));
    WAR << str::form("'%s' not found", name.c_str()) << endl;
    zypper.setExitCode(ZYPPER_EXIT_INF_CAP_NOT_FOUND);
    return;
  }
}

// ----------------------------------------------------------------------------

static void
mark_by_name (Zypper & zypper,
              bool install_not_remove,
              const ResObject::Kind &kind,
              const string &name,
              const string & repo = "")
{
  if (install_not_remove)
    mark_for_install(zypper, kind, name, repo);
  else
    mark_for_uninstall(zypper, kind, name);
}


// don't try NAME-EDITION yet, could be confused by
// dbus-1-x11, java-1_4_2-gcj-compat, ...
/*
bool mark_by_name_edition (...)
  static const regex rx_name_edition("(.*?)-([0-9].*)");

  smatch m;
  if (! is_cap && regex_match (capstr, m, rx_name_edition)) {
    capstr = m.str(1) + " = " + m.str(2);
    is_cap = true;
  }
*/

// ----------------------------------------------------------------------------

static void
mark_by_capability (Zypper & zypper,
                    bool install_not_remove,
                    const ResKind & kind,
                    const Capability & cap)
{
  if (!cap.empty()) {
    DBG << "Capability: " << cap << endl;

    Resolver_Ptr resolver = zypp::getZYpp()->resolver();
    if (install_not_remove) {
      DBG << "Adding requirement " << cap << endl;
      resolver->addRequire (cap);
    }
    else {
      DBG << "Adding conflict " << cap << endl;
      resolver->addConflict (cap);
    }
  }
}

// ----------------------------------------------------------------------------

// join arguments at comparison operators ('=', '>=', and the like)
static void
install_remove_preprocess_args(const Zypper::ArgList & args,
                               Zypper::ArgList & argsnew)
{
  Zypper::ArgList::size_type argc = args.size();
  argsnew.reserve(argc);
  string tmp;
  // preprocess the arguments
  for(Zypper::ArgList::size_type i = 0, lastnew = 0; i < argc; ++i)
  {
    tmp = args[i];
    if (i
        && (tmp == "=" || tmp == "==" || tmp == "<"
            || tmp == ">" || tmp == "<=" || tmp == ">=")
        && i < argc - 1)
    {
      argsnew[lastnew-1] += tmp + args[++i];
      continue;
    }
    else if (tmp.find_last_of("=<>") == tmp.size() - 1 && i < argc - 1)
    {
      argsnew.push_back(tmp + args[++i]);
      ++lastnew;
    }
    else if (i && tmp.find_first_of("=<>") == 0)
    {
      argsnew[lastnew-1] += tmp;
      ++i;
    }
    else
    {
      argsnew.push_back(tmp);
      ++lastnew;
    }
  }

  DBG << "old: ";
  copy(args.begin(), args.end(), ostream_iterator<string>(DBG, " "));
  DBG << endl << "new: ";
  copy(argsnew.begin(), argsnew.end(), ostream_iterator<string>(DBG, " "));
  DBG << endl;
}

// ----------------------------------------------------------------------------

static void
mark_selectable(Zypper & zypper,
                ui::Selectable & s,
                bool install_not_remove,
                bool force,
                const string & repo = "",
                const string & arch = "")
{
#if USE_THE_ONE
  PoolItem theone = s.theObj();
#else
  PoolItem theone;
  if (s.installedEmpty())
    theone = *s.availableBegin();
  else
    theone = findUpdateItem(God->pool(), *s.installedBegin());

  if (!theone)
    theone = *s.installedBegin();
#endif

  DBG << "the One: " << theone << endl;

  //! \todo handle multiple installed case
  bool theoneinstalled; // is the One installed ?
  if (!traits::isPseudoInstalled(s.kind()))
    theoneinstalled = !s.installedEmpty() &&
      equalNVRA(*s.installedObj().resolvable(), *theone.resolvable());
  else if (s.kind() == ResKind::patch)
    theoneinstalled = theone.isRelevant() && theone.isSatisfied();
  else
    theoneinstalled = theone.isSatisfied();

  bool anyinstalled = theoneinstalled;
  PoolItem installed;
  if (theoneinstalled)
    installed = theone;
  else
  {
    if (!traits::isPseudoInstalled(s.kind()))
    {
      anyinstalled = s.hasInstalledObj();
      installed = s.installedObj();
    }
    else if (s.kind() == ResKind::patch)
    {
      for_(it, s.availableBegin(), s.availableEnd())
      {
        if (it->isRelevant() && it->isSatisfied())
        {
          if (installed)
          {
            if (it->resolvable()->edition() > installed->edition())
              installed = *it;
          }
          else
          {
            installed = *it;
            anyinstalled = true;
          }
        }
      }
    }
    else
    {
      for_(it, s.availableBegin(), s.availableEnd())
      {
        if (it->status().isSatisfied())
        {
          if (installed)
          {
            if (it->resolvable()->edition() > installed->edition())
              installed = *it;
          }
          else
          {
            installed = *it;
            anyinstalled = true;
          }
        }
      }
    }
  }

  if (install_not_remove)
  {
    if (theoneinstalled && !force)
    {
      DBG << "the One (" << theone << ") is installed, skipping." << endl;
      zypper.out().info(str::form(
          _("'%s' is already installed."), s.name().c_str()));
      return;
    }

    if (theoneinstalled && force)
    {
      s.setStatus(ui::S_Install);
      DBG << s << " install: forcing reinstall" << endl;
    }
    else
    {
      Capability c;

      // require version greater than the installed. The solver should pick up
      // the latest installable in this case, which is what we want for all
      // kinds (and for patches having the latest installed should automatically
      // satisfy all older version, or make them irrelevant)

      //! could be a problem for patches if there would a greater version
      //! of a patch appear that would be irrelevant at the same time. Should
      //! happen probably.
      if (anyinstalled)
        c = Capability(s.name(), Rel::GT, installed->edition(), s.kind());
      // require any version
      else
        c = Capability(s.name(), s.kind());

      God->resolver()->addRequire(c);
      DBG << s << " install: adding requirement " << c << endl;
    }
  }
  // removing is simpler, as usually
  //! \todo but not that simple - simply adding a conflict with a pattern
  //! or patch does not make the packages it requires to be removed.
  //! we still need to define what response of zypper -t foo bar should be for PPP
  else
  {
    if (!anyinstalled)
    {
      zypper.out().info(str::form(
          _("'%s' is not installed."), s.name().c_str()));
      DBG << s << " remove: not installed, skipping." << endl;
      return;
    }

    Capability c(s.name(), s.kind());
    God->resolver()->addConflict(c);
    DBG << s << " remove: adding conflict " << c << endl;
  }
}

// ----------------------------------------------------------------------------

void install_remove(Zypper & zypper,
                    const Zypper::ArgList & args,
                    bool install_not_remove,
                    const ResKind & kind)
{
  if (args.empty())
    return;

  if (kind == ResKind::patch && !install_not_remove)
  {
    zypper.out().error(
        _("Cannot uninstall patches."),
        _("Installed status of a patch is determined solely based on its dependencies.\n"
          "Patches are not installed in sense of copied files, database records,\n"
          "or similar."));
    zypper.setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
    throw ExitRequestException("not implemented");
  }

  if (kind == ResKind::pattern && !install_not_remove)
  {
    //! \todo define and implement pattern removal (bnc #407040)
    zypper.out().error(
        _("Uninstallation of a pattern is currently not defined and implemented."));
    zypper.setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
    throw ExitRequestException("not implemented");
  }

  bool force_by_capability = zypper.cOpts().count("capability");
  bool force_by_name = zypper.cOpts().count("name");
  bool force = zypper.cOpts().count("force");
  if (force)
    force_by_name = true;

  if (force_by_capability && force_by_name)
  {
    zypper.out().error(boost::str(
      format(_("%s contradicts %s")) % "--capability" % (force? "--force" : "--name")));

    zypper.setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
    ZYPP_THROW(ExitRequestException());
  }

  if (install_not_remove && force_by_capability && force)
  {
    // translators: meaning --force with --capability
    zypper.out().error(boost::str(format(_("%s cannot currently be used with %s"))
      % "--force" % "--capability"));
    zypper.setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
    ZYPP_THROW(ExitRequestException());
  }

  Zypper::ArgList argsnew;
  install_remove_preprocess_args(args, argsnew);

  string str, arch, repo;
  bool by_capability;
  const ResPool & pool = God->pool();
  for_(it, argsnew.begin(), argsnew.end())
  {
    str = *it; arch.clear(); repo.clear();
    by_capability = false;

    // install remove modifiers
    if (str[0] == '+' || str[0] == '~')
    {
      install_not_remove = true;
      str.erase(0, 1);
    }
    else if (str[0] == '-' || str[0] == '!')
    {
      install_not_remove = false;
      str.erase(0, 1);
    }

    string::size_type pos;

    if ((pos = str.rfind(':')) != string::npos)
    {
      repo = str.substr(0, pos);
      str = str.substr(pos + 1);
      force_by_name = true; // until there is a solver API for this
    }

    //! \todo force arch with '.' or '@'???
    if ((pos = str.find('.')) != string::npos)
    { }

    // mark by name by force
    if (force_by_name)
    {
      mark_by_name (zypper, install_not_remove, kind, str, repo);
      continue;
    }

    // recognize missplaced command line options given as packages
    // bnc#391644
    if ( str[0] == '-' )
    {
      // FIXME show a message here, after string freeze
      //zypper.out().error(boost::str(format(_("%s is not a valid package or capability name.") % str));
      zypper.out().error(_("No valid arguments specified."));
      zypper.setExitCode(ZYPPER_EXIT_ERR_INVALID_ARGS);
      ZYPP_THROW(ExitRequestException());
    }


    // is version specified?
    by_capability = str.find_first_of("=<>") != string::npos;

    // try to find foo-bar-1.2.3-2
    if (!by_capability && str.find('-') != string::npos)
    {
      // try to find the original string first as name
      // search by name, not in whatprovides, since there can be provider even
      // for cracklib-dict=small
      // continue only if nothing has been found this way
      if (pool.byIdentBegin(kind, str) == pool.byIdentEnd(kind, str))
      {
        // try to replace '-' for '=' from right to the left and check
        // whether there is something providing such capability
        string::size_type pos = string::npos;
        while ((pos = str.rfind('-', pos)) != string::npos)
        {
          string trythis = str;
          trythis.replace(pos, 1, 1, '=');
          string tryver = str.substr(pos + 1, str.size() - 1);

          DBG << "trying: " << trythis << " edition: " << tryver << endl;

          Capability cap = safe_parse_cap (zypper, trythis, kind);
          sat::WhatProvides q(cap);
          for_(sit, q.begin(), q.end())
          {
            if (sit->edition().match(tryver) == 0)
            {
              str = trythis;
              by_capability = true;
              DBG << str << "might be what we wanted" << endl;
              break;
            }
          }
          if (by_capability)
            break;
          --pos;
        }
      }
    }

    // try to find by name + wildcards first
    if (!by_capability)
    {
      PoolQuery q;
      q.addKind(kind);
      q.addAttribute(sat::SolvAttr::name, str);
      q.setMatchGlob();
      bool found = false;
      for_(s, q.selectableBegin(), q.selectableEnd())
      {
        mark_selectable(zypper, **s, install_not_remove, force);
        found = true;
      }
      // done with this requirement, skip to next argument
      if (found)
        continue;
    }

    // try by capability

    Capability cap = safe_parse_cap (zypper, str, kind);
    sat::WhatProvides q(cap);

    // is there a provider for the requested capability?
    if (q.empty())
    {
      // translators: meaning a package %s or provider of capability %s
      zypper.out().error(str::form(_("'%s' not found."), str.c_str()));
      WAR << str::form("'%s' not found", str.c_str()) << endl;
      zypper.setExitCode(ZYPPER_EXIT_INF_CAP_NOT_FOUND);
      if (zypper.globalOpts().non_interactive)
        ZYPP_THROW(ExitRequestException());
      continue;
    }

    // is the provider already installed?
    bool installed = false;
    string provider;
    for_(solvit, q.poolItemBegin(), q.poolItemEnd())
    {
      if (traits::isPseudoInstalled(solvit->resolvable()->kind()))
        installed = solvit->isSatisfied();
      else
        installed = solvit->status().isInstalled();
      if (installed)
      {
        provider = solvit->resolvable()->name();
        break;
      }
    }

    // already installed, nothing to do
    if (installed && install_not_remove)
    {
      if (provider == str)
        zypper.out().info(str::form(
            _("'%s' is already installed."), str.c_str()));
      else
        zypper.out().info(str::form(
            // translators: %s are package names
            _("'%s' providing '%s' is already installed."),
            provider.c_str(), str.c_str()));

      MIL << str::form("skipping '%s': already installed", str.c_str()) << endl;
      continue;
    }
    // not installed, nothing to do
    else if (!installed && !install_not_remove)
    {
      // translators: meaning a package %s or provider of capability %s
      zypper.out().info(str::form(_("'%s' is not installed."), str.c_str()));
      MIL << str::form("skipping '%s': not installed", str.c_str()) << endl;
      continue;
    }

    mark_by_capability (zypper, install_not_remove, kind, cap);
  }
}

