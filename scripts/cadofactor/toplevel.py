#!/usr/bin/env python3

# This is the hairy bit of logic which emulates the factor.sh of old.  We
# put all of this in a class so that we can doctest it more conveniently

import re
import os
import sys
import subprocess
import locale
import logging
import tempfile
import argparse
import cadologger
import cadoparams
import shutil

# I'm having weird problems with the doctests, which seem to almost never obey
# the log level I intend to set up. Manually setting the loglevel of the
# toplevel logger structure is ok, though.

class Cado_NFS_toplevel(object):
    ''' Class with only a few methods to deal with top-level parameter
    fiddling.
    '''

    def find_default_parameter_file(self):
        ''' return the full path of the default parameter file which
        is appropriate for the given integer self.args.N. We look for the file
        whose name exactly matches the size in digits of self.args.N, as
        well as the nearest multiple of 5.

        If self.args.dlp is set, we look in the dlp/ subdirectory of
        self.pathdict["data"]. Otherwise we look in the factor/ subdirectory.

        >>> tempdir=tempfile.mkdtemp()
        >>> names = ["c10", "c13", "c20", "p20", "p13" ]
        >>> os.symlink(".", os.path.join(tempdir, "factor"))
        >>> os.symlink(".", os.path.join(tempdir, "dlp"))
        >>> for n in names:
        ...  fn=os.path.join(tempdir, "params." + n)
        ...  f=open(fn, "w")
        ...  f.writelines("# hello, world\\n")
        ...  f.close()
        >>> t = Cado_NFS_toplevel(args=['--screenlog', 'WARNING'])

        # weird, really
        >>> t.logger.setLevel(logging.WARNING)
        >>> t.pathdict["data"] = tempdir
        >>> n10=1234567891
        >>> n12=123456789012
        >>> n13=1234567890123
        >>> n14=12345678901234
        >>> n18=123456789012345678
        >>> n20=12345678901231231231
        >>> n21=123456789012312312311
        >>> t.args.N=n10
        >>> t.args.dlp=False
        >>> fn=t.find_default_parameter_file()
        >>> fn == os.path.join(tempdir, "factor/params.c10")
        True

        >>> t.args.N=n13
        >>> t.args.dlp=False
        >>> fn=t.find_default_parameter_file()
        >>> fn == os.path.join(tempdir, "factor/params.c13")
        True

        >>> t.args.N=n14
        >>> t.args.dlp=False
        >>> try:
        ...   t.find_default_parameter_file()
        ... except RuntimeError:
        ...   'NOTFOUND'
        'NOTFOUND'

        >>> t.args.N=n18
        >>> t.args.dlp=False
        >>> fn=t.find_default_parameter_file()
        >>> fn == os.path.join(tempdir, "factor/params.c20")
        True

        >>> t.args.N=n18
        >>> t.args.dlp=True
        >>> fn=t.find_default_parameter_file()
        >>> fn == os.path.join(tempdir, "dlp/params.p20")
        True

        >>> t.args.N=n13
        >>> t.args.dlp=True
        >>> fn=t.find_default_parameter_file()
        >>> fn == os.path.join(tempdir, "dlp/params.p13")
        True

        >>> t.args.N=n14
        >>> t.args.dlp=True
        >>> try:
        ...   t.find_default_parameter_file()
        ... except RuntimeError:
        ...   'NOTFOUND'
        'NOTFOUND'

        >>> os.unlink(os.path.join(tempdir, "factor"))
        >>> os.unlink(os.path.join(tempdir, "dlp"))
        >>> for n in names:
        ...  fn=os.path.join(tempdir, "params." + n)
        ...  os.unlink(fn)
        >>> os.rmdir(tempdir)
        '''

        default_param_dir = self.pathdict["data"]
        if self.args.dlp:
            default_param_dir = os.path.join(default_param_dir, "dlp")
            letter="p"
        else:
            default_param_dir = os.path.join(default_param_dir, "factor")
            letter="c"
        size_of_n=len(repr(self.args.N))
        # also attempt nearest multiple of 5.
        attempts=[letter+"%d"%x for x in [size_of_n, ((size_of_n+2)//5)*5]]
        if self.args.gfpext:
            attempts=[x+"dd%d"%self.args.gfpext for x in attempts]
        if attempts[1]==attempts[0]:
            attempts=attempts[:1]
        self.logger.debug("Looking for parameter file"
                " for %s in directory %s" % (attempts[0], default_param_dir))
        for f in attempts:
            if os.path.isfile(os.path.join(default_param_dir,"params."+f)):
                paramfile=os.path.join(default_param_dir,"params."+f)
                self.logger.info("Using default parameter file %s" % paramfile)
                return paramfile
        raise RuntimeError("no parameter file found"
                " for %s (tried %s)" % (attempts[0], ", ".join(attempts)))

    @staticmethod
    def number_of_physical_cores():
        ''' return the number of physical cores on the current system. If
        the NCPUS_FAKE environment variable is set, return that instead.

        We tried to make sure that on the relevant systems, we indeed
        return the number of physical cores (not hyperthreaded ones), but
        it's not always easy to be sure.

        >>> type(Cado_NFS_toplevel.number_of_physical_cores())
        <class 'int'>

        >>> Cado_NFS_toplevel.number_of_physical_cores() > 0
        True

        >>> os.environ["NCPUS_FAKE"]="3"
        >>> Cado_NFS_toplevel.number_of_physical_cores()
        3

        >>> del os.environ["NCPUS_FAKE"]
        '''

        try:
            return int(os.environ["NCPUS_FAKE"])
        except KeyError:
            pass
        if os.path.isfile("/proc/cpuinfo"):
            f=open("/proc/cpuinfo")
            lines=f.readlines()
            f.close()
            nphysical=len([x for x in lines if re.match("physical", x)])
            if nphysical == 0:
                return len([x for x in lines if re.match("processor", x)])
            cpu_cores={}
            for x in lines:
                foo=re.match("cpu cores\s*=\s*(\d+)", x)
                if foo:
                    cpu_cores.add(foo.group(1))
            if len(cpu_cores) == 0:
                return len([x for x in lines if re.match("processor", x)])
            if len(cpu_cores) != 1:
                raise ValueError("inhomogeneous platform ?")
            return nphysical * len(cpu_cores)
        def backquote(cmd):
            pipe = subprocess.Popen(cmd.split(" "), stdout=subprocess.PIPE)
            loc = locale.getdefaultlocale()[1]
            return [s.decode(loc) for s in pipe.stdout.readlines()]
        if os.uname().sysname == "OpenBSD":
            # does this count hyperthreading or not ?
            return int(backquote("sysctl -n hw.ncpu")[0])
        if os.uname().sysname == "Darwin":
            # does this count hyperthreading or not ?
            return int(backquote("sysctl -n hw.ncpu")[0])
        if os.uname().sysname == "MINGW32_NT-6.1":
            # not clear whether it's physical or logical.
            crap=backquote("wmic cpu get Caption")
            return max(len(crap)-1,1)
            # we don't believe mingw will support multithreading well.
        # this would work as well on linux and darwin, but pretty surely does
        # not count hyperthreading. Does not work on openbsd5.3
        return max(int(backquote("getconf _NPROCESSORS_ONLN")[0]), 1)

    def filter_out_N_paramfile_workdir(self):
        ''' This function takes, and modifies, the namespace self.args
        which has been returned by parser.parse_args(). It looks for
        "special" arguments among the positional arguments in
        self.args.options, and take them out to get a different status.

        As options normally follow the syntax key=value, we can filter
        out some kinds of arguments, as follows.

         - Only-digits options, or options matching N=(\d+), end up in
           self.args.N ; of course only one such is allowed.
         - Paths to existing files are understood as names of parameter
           files (which can also be passed using --parameters), which
           eventually go to self.args.parameter. AT THE MOMENT only one
           parameter file is allowed, but it would not seem absurd to
           allow several.
         - Paths to existing directories, or paths to places where a
           directory can be created, are understood as giving the
           working directory location. Note that the working directory
           location can be passed using --workdir too, as well as with
           tasks.workdir. Of course, among these, only one directory
           location is allowed. It is eventually stored in
           self.args.workdir

        This logic is admittedly somewhat ugly, but we really need it in
        order to have a human-usable toplevel script which does the right
        thing without requireing arcane config options. 

        >>> fd,name=tempfile.mkstemp(text=True)
        >>> paramfile=os.fdopen(fd, 'w')
        >>> paramfile.writelines(['tasks.workdir=/tmp\\n'])
        >>> paramfile.close()
        >>> t = Cado_NFS_toplevel(args=['12345', name])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.args.N
        12345

        >>> t.args.parameters == name
        True

        The parameter file has tasks.workdir = /tmp, but it is important
        to understand that the mission of this function is not to read it
        -- yet. This is done by set_N_paramfile_workdir
        >>> t.args.workdir is None
        True

        Make sure that we want to have only one N argument
        >>> t = Cado_NFS_toplevel(args=['N=12345', '6789'])
        >>> try:
        ...  t.filter_out_N_paramfile_workdir()
        ... except ValueError:
        ...  'ok'
        'ok'

        Make sure that we want to have only one parameter argument. NOTE
        that this behaviour could change.
        >>> t = Cado_NFS_toplevel(args=[name, '-p', name])
        >>> try:
        ...  t.filter_out_N_paramfile_workdir()
        ... except ValueError:
        ...  'ok'
        'ok'

        Make sure that we want to have only one workdir argument
        >>> t = Cado_NFS_toplevel(args=[name+".d", '-w', name+".d"])
        >>> try:
        ...  t.filter_out_N_paramfile_workdir()
        ... except ValueError:
        ...  'ok'
        'ok'

        >>> t = Cado_NFS_toplevel(args=[name])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.args.N is None
        True

        >>> t.args.parameters == name
        True

        Path where a directory can be created is understood as workdir
        >>> t = Cado_NFS_toplevel(args=[name, name + ".d"])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.args.workdir == name + ".d"
        True

        Existing directory path understood as workdir
        >>> t = Cado_NFS_toplevel(args=["/tmp"])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.args.workdir == "/tmp"
        True

        >>> os.unlink(name)
        '''

        equal_options=[]
        supplied_N=[]
        supplied_parameters=[]
        supplied_workdir=[]
        if self.args.workdir:
            supplied_workdir.append(self.args.workdir)
        if self.args.parameters:
            supplied_parameters.append(self.args.parameters)
        self.args.workdir=None
        self.args.parameters=None
        self.args.N=None
        for x in self.args.options:
            matches=[]
            def match_and_store(matches, pattern, string):
                # matches.clear() is not for all python versions.
                del matches[:]
                foo=re.match(pattern, string)
                if foo:
                    matches += foo.groups()
                return foo
            if match_and_store(matches, "^N=(\d+)$",x):
                supplied_N.append(matches[0])
            elif match_and_store(matches, "^(\d+)$",x):
                supplied_N.append(matches[0])
            elif len(supplied_parameters) == 0 and os.path.isfile(x):
                supplied_parameters.append(x)
            elif len(supplied_workdir) == 0 and (os.path.isdir(x) or not os.path.exists(x) and os.path.exists(os.path.dirname(x))):
                supplied_workdir.append(x)
            elif match_and_store(matches, "(?:tasks\.)?workdir=(.*)", x):
                supplied_workdir.append(matches[0])
            elif re.search("=",x):
                if re.match("tasks.threads=(.*)", x):
                    if self.args.server_threads:
                        raise ValueError("--server-threads conflicts with " +x)
                elif re.match("tasks\.(sieve|polyselect)(\.\w+)*\.threads=(\.*)", x):
                    if self.args.client_threads:
                        raise ValueError("--client-threads conflicts with " + x)
                equal_options.append(x)
            else:
                raise ValueError("free-form argument not understood: %s" % x)
        # N can be given either as N= in the options, or as a free-form
        # integer. Check that this happens only once.
        if len(supplied_N) > 1:
            raise ValueError("Total of free-form N in command line"
                    " and N= options cannot exceed 1")
        elif len(supplied_N):
            self.args.N=int(supplied_N[0])
        # parameter files may be given either as a free-form existing file, or
        # with the --parameters arguments.
        if len(supplied_parameters) > 1:
            raise ValueError("Total of free-form files in command line"
                    " and --parameters argument cannot exceed 1")
        elif len(supplied_parameters):
            self.args.parameters=supplied_parameters[0]
        # workdir may be given either as free-form directory path (either
        # existing directories, or paths for which a directory can be
        # created), with the --workdir argument, or as tasks.workdir= or
        # workdir= in the OPTIONS (for backwards compatibility)
        if len(supplied_workdir) > 1:
            raise ValueError("Total of free-form directory paths in"
                    " command line and --workdir argument cannot exceed 1")
        elif len(supplied_workdir):
            self.args.workdir=supplied_workdir[0]
        if self.args.ell:
            equal_options.append("ell=%s"%self.args.ell)
        self.args.options=equal_options

    def set_N_paramfile_workdir(self):
        '''This sets some parameters based on what we have on the command
        line. Once the location of the parameter file has been decided, this
        function reads it as well

        We also set self.using_default_parameter_file if we are, errr, using
        a default parameter file (this is done to detect very short command
        lines)

        Beyond self.using_default_parameter_file, all the variables set
        by this functions are stored in the hierarchical structure
        self.parameters.

        >>> tempdir=tempfile.mkdtemp()
        >>> os.mkdir(os.path.join(tempdir, "factor"))
        >>> fd,name=tempfile.mkstemp(text=True)
        >>> c5=os.path.join(tempdir, "factor", "params.c5")
        >>> f=open(c5, "w")
        >>> f.writelines(['a.b.c=32\\n'])
        >>> f.close()
        >>> foo=os.path.join(tempdir, "foo")
        >>> f=open(foo, "w")
        >>> f.writelines(['N=67890\\n', 'name=blabla\\n', 'a.b.c=32\\n', 'tasks.workdir=/tmp/somedirectory\\n'])
        >>> f.close()

        N given, no parameter file provided. We want to make sure that we
        are going to read our default parameter file.
        >>> t = Cado_NFS_toplevel(args=['12345'])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.setpath("data", tempdir)
        >>> t.parameters = cadoparams.Parameters()
        >>> t.set_N_paramfile_workdir()
        >>> t.parameters.get_simple("N", 0)
        12345

        >>> t.parameters.get_simple("a.b.x.y.c", 0)
        32

        >>> os.rmdir(t.parameters.get_simple("tasks.workdir"))
        >>> t.using_default_parameter_file
        True

        N given, no parameter file provided, but working directory
        override present. We want to make sure that we are going to read
        our default parameter file.
        >>> t = Cado_NFS_toplevel(args=['12345', '/tmp'])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.setpath("data", tempdir)
        >>> t.parameters = cadoparams.Parameters()
        >>> t.set_N_paramfile_workdir()
        >>> t.parameters.get_simple("N", 0)
        12345

        >>> t.parameters.get_simple("a.b.x.y.c", 0)
        32

        >>> t.parameters.get_simple("tasks.workdir")
        '/tmp'

        >>> t.using_default_parameter_file
        True

        N and parameter file provided. We complain if this parameter file
        also defines N (to a different value, that is).
        MAYBE this could be accepted.
        >>> t = Cado_NFS_toplevel(args=['12345', foo])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.setpath("data", tempdir)
        >>> t.parameters = cadoparams.Parameters()
        >>> try:
        ...  t.set_N_paramfile_workdir()
        ... except ValueError:
        ...  'ok'
        'ok'

        We want to make sure that we are using the good default parameter
        file, and that we're
        >>> t = Cado_NFS_toplevel(args=['67890', foo])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.setpath("data", tempdir)
        >>> t.parameters = cadoparams.Parameters()
        >>> t.set_N_paramfile_workdir()
        >>> t.parameters.get_simple("N", 0)
        67890

        >>> t.parameters.get_simple("tasks.workdir")
        '/tmp/somedirectory'

        >>> t.using_default_parameter_file
        False

        Just a parameter file provided. Make sure N and workdir are setup
        properly
        >>> t = Cado_NFS_toplevel(args=[foo])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.parameters = cadoparams.Parameters()
        >>> t.set_N_paramfile_workdir()
        >>> t.parameters.get_simple('N', 0)
        67890

        >>> t.parameters.get_simple('name')
        'blabla'

        >>> t.parameters.get_simple('tasks.workdir')
        '/tmp/somedirectory'

        Parameter file with no N provided. Complain
        >>> t = Cado_NFS_toplevel(args=[c5])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.parameters = cadoparams.Parameters()
        >>> try:
        ...  t.set_N_paramfile_workdir()
        ... except ValueError as e:
        ...  bool(re.search("must define N", str(e)))
        True

        Parameter file and workdir provided.
        >>> cadoparams.logger.setLevel(logging.CRITICAL)
        >>> t = Cado_NFS_toplevel(args=[foo, '/tmp'])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.parameters = cadoparams.Parameters()
        >>> t.set_N_paramfile_workdir()
        >>> t.parameters.get_simple('tasks.workdir')
        '/tmp'

        >>> os.unlink(c5)
        >>> os.rmdir(os.path.join(tempdir, "factor"))
        >>> os.unlink(foo)
        >>> os.rmdir(tempdir)
        '''
        # The top-level logic which depends on N, parameters, and workdir
        # is as follows.
        #
        # N given:
        #   - use the parameters file if provided on the command line (either
        #     with -p or as a standalone file). If none provided, choose one
        #     from the set of default shipped parameter files, rounding to
        #     the nearest multiple of 5 if needed
        #   - use the working directory if provided on the command line
        #     (either with -p or as a standalone directory name). Create it
        #     if it does not exist.
        #   - If no working directory provided, use the working
        #     directory name provided within the parameter file. Create it if
        #     it does not exist.
        #   - If no working directory decided from the means above, create a
        #     temporary directory. Delete it at the end if the computation
        #     succeeds and CADO_DEBUG is unset
        #   - start or resume computation from the working directory, with
        #     the chosen parameters.
        # 
        # N not given:
        #   - a parameter file must be given. It must contain N.
        #   - a working directory is optional. If provided, this directory is
        #     used. If not provided, the one from the parameter file is used.
        #     If none there, create a temporary directory. Delete it at the
        #     end if the computation succeeds and CADO_DEBUG is unset

        if self.args.N:
            if not self.args.parameters:
                self.args.parameters = self.find_default_parameter_file()
                self.using_default_parameter_file = True
            self.parameters.readfile(self.args.parameters)
            # make sure there's no inconsistency with a previously set N,
            # which could be in the parameter file.
            previous_N=self.parameters.get_simple("tasks.N", self.args.N)
            if self.args.N != previous_N:
                raise ValueError("given N differs from N stored in %s" % self.args.parameters)
            self.parameters.set_simple("N", self.args.N)
        else:
            if not self.args.parameters:
                raise ValueError("if N is not given, a parameter file is required")
            self.parameters.readfile(self.args.parameters)
            if not self.parameters.get_simple("N", 0):
                raise ValueError("%s must define N" % self.args.parameters)

        if self.args.workdir:
            self.parameters.set_simple("tasks.workdir", self.args.workdir)
        try:
            wdir=self.parameters.get_simple("tasks.workdir")
        except KeyError:
            wdir=tempfile.mkdtemp(prefix="cado.")
            self.logger.info("Created temporary directory %s" % wdir)
            self.parameters.set_simple("tasks.workdir",wdir)
            self.purge_files.append(wdir)
            if os.environ.get("CADO_DEBUG", None):
                self.logger.info("CADO_DEBUG is on, data will be kept in %s" % wdir)

        if not os.path.isdir(wdir):
            self.logger.debug("Created directory %s" % wdir)
            os.makedirs(wdir)

    def set_threads_and_client_threads(self):
        ''' This function processes the --client-threads and
        --server-threads arguments, and sets the parameters
        tasks.threads, tasks.polyselect.threads, and
        tasks.sieve.threads

        >>> os.environ["NCPUS_FAKE"]="3"
        >>> t = Cado_NFS_toplevel(args=['12345', '-p', os.path.os.devnull, '--server-threads', 'all'])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.parameters = cadoparams.Parameters()
        >>> t.set_N_paramfile_workdir()
        >>> t.parameters.readparams(t.args.options)
        >>> t.set_threads_and_client_threads()
        >>> t.parameters.get_simple("tasks.threads",0)
        3

        >>> t.parameters.get_simple("tasks.sieve.threads",0)
        2

        >>> t.parameters.get_simple("tasks.polyselect.threads",0)
        2

        >>> os.environ["NCPUS_FAKE"]="3"
        >>> t = Cado_NFS_toplevel(args=['12345', '-p', os.path.os.devnull, '--server-threads', 'all', '--client-threads', '1'])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.parameters = cadoparams.Parameters()
        >>> t.set_N_paramfile_workdir()
        >>> t.parameters.readparams(t.args.options)
        >>> t.set_threads_and_client_threads()
        >>> t.parameters.get_simple("tasks.threads",0)
        3

        >>> t.parameters.get_simple("tasks.sieve.threads",0)
        1

        >>> t.parameters.get_simple("tasks.polyselect.threads",0)
        1

        >>> t = Cado_NFS_toplevel(args=['12345', '-p', os.path.os.devnull, '--server-threads', '4'])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.parameters = cadoparams.Parameters()
        >>> t.set_N_paramfile_workdir()
        >>> t.parameters.readparams(t.args.options)
        >>> t.set_threads_and_client_threads()
        >>> t.parameters.get_simple("tasks.threads",0)
        4

        >>> t.parameters.get_simple("tasks.sieve.threads",0)
        2

        >>> t.parameters.get_simple("tasks.polyselect.threads",0)
        2

        >>> t = Cado_NFS_toplevel(args=['12345', '-p', os.path.os.devnull, '--server-threads', '1'])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.parameters = cadoparams.Parameters()
        >>> t.set_N_paramfile_workdir()
        >>> t.parameters.readparams(t.args.options)
        >>> t.set_threads_and_client_threads()
        >>> t.parameters.get_simple("tasks.threads",0)
        1

        >>> t.parameters.get_simple("tasks.sieve.threads",0)
        1

        >>> t.parameters.get_simple("tasks.polyselect.threads",0)
        1

        Default is the same as --server-threads all
        >>> t = Cado_NFS_toplevel(args=['12345', '-p', os.path.os.devnull])
        >>> t.filter_out_N_paramfile_workdir()
        >>> t.parameters = cadoparams.Parameters()
        >>> t.set_N_paramfile_workdir()
        >>> t.parameters.readparams(t.args.options)
        >>> t.set_threads_and_client_threads()
        >>> t.parameters.get_simple("tasks.threads",0)
        3

        >>> t.parameters.get_simple("tasks.sieve.threads",0)
        2

        >>> t.parameters.get_simple("tasks.polyselect.threads",0)
        2

        >>> del os.environ["NCPUS_FAKE"]

        '''
        t=self.parameters.get_simple("tasks.threads", 0)
        if not t and not self.args.server_threads:
            # by default, use all threads on server
            self.args.server_threads = 'all'
        if self.args.server_threads:
            if self.args.server_threads == "all":
                ncpus = self.number_of_physical_cores()
                self.args.server_threads = ncpus
                self.logger.info("Set tasks.threads=%d based on detected physical cpus" % ncpus)
            t=int(self.args.server_threads)
            self.parameters.set_simple("tasks.threads", t)
        # We want to enforce a value for tasks.polyselect.threads and
        # tasks.sieve.threads. If there is none safe by the side-effect
        # of having set tasks.threads, we want to set our default.
        if self.args.client_threads:
            c=self.args.client_threads
            self.parameters.set_simple("tasks.polyselect.threads", c)
            self.parameters.set_simple("tasks.sieve.threads", c)
        else:
            t=self.parameters.get_simple("tasks.threads", 0)
            for p in ["tasks.polyselect.threads", "tasks.sieve.threads"]:
                if self.parameters.locate(p) == "tasks.threads":
                    self.parameters.set_simple(p, min(t, 2))
        for p in ["tasks.polyselect.threads", "tasks.sieve.threads"]:
            self.logger.info("%s = %s" % (p, self.parameters.get_simple(p)))
            assert self.parameters.locate(p) == p


    def set_slaves_parameters(self):
        ''' sets slaves.nrclients and slaves.scriptpath to default values
        >>> t = Cado_NFS_toplevel(args=['-p', os.path.os.devnull, '12345', 'slaves.hostnames=foo,bar', 'slaves.scriptpath=/tmp'])
        >>> t.setpath("lib", "/tmp")
        >>> p = t.get_cooked_parameters()
        >>> p.get_simple("slaves.nrclients", 0)
        1

        If we are run with a non-default parameter file, then we end up
        running in pure server mode.
        >>> os.environ["NCPUS_FAKE"]="4"
        >>> t = Cado_NFS_toplevel(args=['-p', os.path.os.devnull, '12345', 'slaves.scriptpath=/tmp'])
        >>> t.setpath("lib", "/tmp")
        >>> p = t.get_cooked_parameters()
        >>> p.get_simple("slaves.nrclients", 0)
        0

        >>> os.environ["NCPUS_FAKE"]="4"
        >>> t = Cado_NFS_toplevel(args=['-p', os.path.os.devnull, '12345', 'slaves.scriptpath=/tmp'])
        >>> t.setpath("lib", "/tmp")

        We are cheating. In order to see what happens in the old
        "factor.sh"-like way, see what happens if we read the old
        parameter file.
        >>> t.using_default_parameter_file=True
        >>> p = t.get_cooked_parameters()
        >>> p.get_simple("slaves.nrclients", 0)
        2

        >>> del os.environ["NCPUS_FAKE"]
        '''
        # Argument --server kills all slaves.* arguments
        if self.args.server:
            for p in self.parameters.find(["slaves"], ""):
                self.logger.warn("server mode, ignoring " + ".".join(p[0]+[p[1]]))
                # actually delete them! Yeah, it's ugly.
                self.parameters.data.pop("slaves", None)
            return
        if self.args.slaves:
            self.parameters.set_if_unset("slaves.nrclients", self.args.slaves)
        if self.parameters.myparams(["hostnames"], "slaves"):
            # default nrclients is 1 if we specify explicit list of slaves.
            self.parameters.set_if_unset("slaves.nrclients", 1)
        elif self.using_default_parameter_file:
            self.parameters.set_if_unset("slaves.hostnames", "localhost")
            self.parameters.set_if_unset("slaves.basepath",
                    os.path.join(self.parameters.get_simple("tasks.workdir",
                        "client")))
            # running on localhost, default nrclients to just as many as
            # needed to use the number of threads which have been asked
            # for.
            t = self.parameters.get_simple("tasks.threads", 0)
            tp = self.parameters.get_simple("tasks.polyselect.threads", 0)
            ts = self.parameters.get_simple("tasks.sieve.threads", 0)
            if t:
                ct = max(tp,ts)
                nrclients=int((t+ct-1)//ct)
                self.parameters.set_if_unset("slaves.nrclients", nrclients)
            else:
                self.parameters.set_if_unset("slaves.nrclients", 1)

        # What is a sensible default value for scriptpath?
        if self.parameters.get_simple("slaves.nrclients", 0):
            if not self.parameters.get_simple("slaves.scriptpath", ""):
                # see whether cado-nfs-client.py exists.
                # try in the source tree. If we have the source tree defined,
                # this is where we are willing to look.

                if self.pathdict.get("source", None):
                    f=os.path.join(self.pathdict["source"],
                            "cado-nfs-client.py")
                    if os.path.isfile(f):
                        self.parameters.set_if_unset("slaves.scriptpath",
                                os.path.abspath(self.pathdict["source"]))

                self.parameters.set_if_unset("slaves.scriptpath",
                        os.path.abspath(self.pathdict["bin"]))
                self.logger.info("slaves.scriptpath is %s" %
                        self.parameters.get_simple("slaves.scriptpath"))
                f=os.path.join(self.parameters.get_simple(
                        "slaves.scriptpath"), "cado-nfs-client.py")
                if not os.path.isfile(f):
                    raise ValueError("Could not find file named %s, cannot run clients" % f)

    @staticmethod
    def default_parser():
        parser = argparse.ArgumentParser(description="Integer factorization"
                " or GF(p) discrete logarithms with the Number Field Sieve")
        parser.add_argument("--screenlog",
                help="Screen logging level, e.g., INFO/COMMAND/DEBUG",
                default="INFO", metavar="LEVEL")
        parser.add_argument("--filelog",
                help="Log file logging level, e.g.,  INFO/COMMAND/DEBUG",
                default="DEBUG", metavar="LEVEL")
        parser.add_argument("--parameters", "-p",
                help="A file with the parameters to use")
        parser.add_argument("options",
                metavar="OPTION",
                help="options as in parameter file (format: key=value)",
                nargs="*")
        parser.add_argument("--workdir", "--wdir", "-w",
                help="Aliases (and conflicts with) tasks.workdir."
                " WORKDIR is created if it does not exist."
                " If unspecified (neither here nor in the parameter files),"
                " a temporary directory is used.")
        parser.add_argument("--client-threads",
                type=int,
                help="Number of threads for sieving and "
                "polynomial selection jobs.")
        parser.add_argument("--server-threads", "-t", 
                metavar='NCORES',
                help="Aliases (and conflicts with) tasks.threads."
                " NCORES may be \"all\", which then is the number"
                " of physical cores of the current node. Argument"
                " --client-threads may also be passed, leading to"
                " finer grain setting. When used to run the factorization"
                " locally, set slaves.nrclient = tasks.threads /"
                " max(task.sieve.threads, tasks.polyselect.threads)")
        parser.add_argument("--slaves", "-s",
                help="Aliases (and conflicts with) slaves.nrclients")
        parser.add_argument("-gfpext",
                type=int,
                default=0,
                help="Degree of the finite field extension (DLP only)")
        parser.add_argument("--ell", "-ell",
                help="Aliases (and conflicts with) ell in parameter files")
        parser.add_argument("--server",
                help="Run a bare server, do not start any clients",
                action='store_true')
        parser.add_argument("-dlp",
                help="Run discrete logarithm computation instead",
                action='store_true')
        parser.add_argument("--verboseparam",
                help="Enable very verbose parameter parsing",
                action='store_true')
        parser.add_argument("--no-colors",
                help="Turn off colored output",
                action='store_true')
        return parser

    def __init__(self, args=None):
        self.using_default_parameter_file = False
        self.parser=self.default_parser()
        self.args = self.parser.parse_args(args)
        # screenlvlname = self.args.screenlog
        # filelvlname = self.args.filelog
        
        screenlvl = getattr(cadologger, self.args.screenlog.upper())
        self.logger = logging.getLogger()
        self.logger.addHandler(cadologger.ScreenHandler(lvl = screenlvl,
            colour=not self.args.no_colors))
        self.purge_files=[]
        self.pathdict=dict()
        # I have some fairly weird behaviour here in the doctests.
        # Apparently we need to set this, or the logging level is just
        # constant...
        # self.logger.setLevel(screenlvl)

    # We don't make this a destructor, because python spews not very nice
    # error messages in this case.
    def purge_temp_files(self, nopurge=False):
        for x in self.purge_files:
            if os.environ.get("CADO_DEBUG", None):
                self.logger.info("CADO_DEBUG is on, data kept in %s" % x)
                pass
            else:
                if nopurge:
                    self.logger.info("Computation data kept in %s" % x)
                else:
                    self.logger.info("Cleaning up computation data in %s" % x)
                    shutil.rmtree(x)

    def get_cooked_parameters(self):
        '''return the parameters hierarchy in a format which is suitable
        for processing by all the other parts of the python code.
        
        >>> os.environ["NCPUS_FAKE"]="4"
        >>> t = Cado_NFS_toplevel(args=['-p', os.path.os.devnull, '12345', 'slaves.hostnames=foo,bar', 'tasks.workdir=/tmp/a', 'slaves.scriptpath=/tmp'])
        >>> t.setpath("lib", "/tmp")
        >>> p = t.get_cooked_parameters()
        >>> print(p)
        N = 12345
        slaves.hostnames = foo,bar
        slaves.nrclients = 1
        slaves.scriptpath = /tmp
        tasks.execpath = /tmp
        tasks.threads = 4
        tasks.workdir = /tmp/a
        tasks.polyselect.threads = 2
        tasks.sieve.threads = 2

        >>> del os.environ["NCPUS_FAKE"]
        '''
        self.filter_out_N_paramfile_workdir()
        self.parameters = cadoparams.Parameters(self.args.verboseparam)
        self.parameters.set_simple("tasks.execpath", self.pathdict["lib"])
        # find out which parameter file to use, and read it
        self.set_N_paramfile_workdir()
        # now use our options to override what is in the parameter file
        self.parameters.readparams(self.args.options)
        self.set_threads_and_client_threads()
        self.set_slaves_parameters()
        return self.parameters

    def setpath(self, key, value):
        self.pathdict[key]=value


if __name__ == "__main__":
    import sys
    if len(sys.argv) == 1:
        import doctest
        doctest.testmod()