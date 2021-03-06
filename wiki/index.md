MUDFiles
========


Overview
--------
My MUDFiles source repository contains several sections/projects:

- [My TinTin++ Codebase](https://github.com/nfarrar/mudfiles/tree/master/ttlib)
- [NannyMUD Server Source](https://github.com/nfarrar/mudfiles/tree/master/nannymud)
- [Pyre](https://github.com/nfarrar/mudfiles/tree/master/pyre) (A Pure Python Programmable Mud Client)
- [Wiki](http://crunk.io/mudfiles) ([source](https://github.com/nfarrar/mudfiles/tree/master/wiki))

Note: This page serves as the primary informational page on the [source repository](https://github.com/nfarrar/mudfiles), as well as the [wiki index page](http://mudfiles.crunk.io).  Based on which resource your viewing, some of the content *may* seem out of context.


Status
------
Some time ago, I started working on 'modularizing' my tintin code. In the process, I stood up the [Unofficial TinTin++ User's Group](https://github.com/tintinplusplus) and dumped some POC code there. I got frustrated working with the TinTin++ language, and started working on a pure python client, but ran into time constraints with work and stopped working on these projects (for a time) and let them fall into desrepair.

I've just started reorganizing my codebase/projects and have centralized them here in a single repository. I've been slowly updating them and am working on a new approach to building modular, reusable TinTin++ code - it's a hybrid solution that leverages a TinTin -> Python API and will hopefully allow advanced TinTin++ users to share their codebases and built more advanced features.


Installation
------------
Setup & usage instructions:

    curl -O https://raw.github.com/pypa/pip/master/contrib/get-pip.py
    python get-pip.py
    pip install virtualenv virtualenvwrapper tmuxp
    mkvirutalenv mudfiles

    git clone https://github.com/nfarrar/mudfiles.git
    cd mudfiles
    pip install -r requirements.txt

    cd ttlib
    ./startin

Note: My mudfiles environment utilizies tintin++, tmux & python. Depending on your platform these instructions will vary.


Wiki
----
The wiki is built using [MDWiki][] and served statically via [Github Pages][]. I've chosen this solution purely for *simplicity*. The notes are written in [Github Flavored Markdown][] (GFM) and converted on the client side to HTML by [MDWiki][]. While [MDWiki][] is extremely straight-forward and easy to use, there are some trade-offs. All the HTML is generated on the client side (by javascript) and there are no preparers or server-side integrations. This makes integrating a search engine difficult - and it also borks the way the content is indexed by google. It also does not fascilitate generating data on the server-side, though this can be done relatively easily using custom scripts.

Much of the data in the [NannyMUD Wiki Section](http:/crunk.io/mudfiles/#!/nannymud/index.md) is incomplete and/or outdated. I'm currently working on Python Integrations with TinTin++ that generate large tables of data in the NannyMUD section (specifically the atlas/realm, healing, item, npc, and quest data) from live client data.

I've also migrated the content from the original [Unofficial TinTin++ Unofficial Documentation](http://tintinplusplus-unoffical-documentation.readthedocs.org/) that I wrote using rst/sphinx to markdown and added it into this wiki (much easier to maintain/update). I've added lots of new content as I've been working on the python integrations and have no intention of updated the original guide.

The wiki can be run locally by downloading the source & starting up any local webserver in the `wiki` directory:

    git clone https://github.com/nfarrar/mudfiles.git
    cd mudfiles
    python -m SimpleHTTPServer


<!-- References -->

[MDWiki]:                   http://mdwiki.info
[Github Pages]:             https://pages.github.com/
[Github Flavored Markdown]: https://help.github.com/articles/github-flavored-markdown/
