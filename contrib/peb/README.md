Quick notes
===========

Create your fileserver directory like this, giving permissions to whatever
user you want to run the fileserver as.

```shell
mkdir /srv/acorn
mkdir /srv/acorn/0ECONET
mkdir /srv/acorn/1STORAGE
```

Then make a little config file. Mine is pgb.config, included:

```
FILESERVER ON 1.254 PATH /srv/acorn
EXPOSE HOST 1.254 ON PORT *:32768
AUN MAP HOST 1.127 ON 10.222.10.146 PORT 32768 NONE
AUN MAP HOST 1.101 ON 127.0.0.1 PORT 32101 NONE
```

Then run the fileserver....

./utilities/econet-hpbridge -c pgb.config -z $*

And go play! Simples.
