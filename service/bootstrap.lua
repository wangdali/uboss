local c = require "uboss.core"

print("uBoss Bootstrap!!!");

c.command("LAUNCH","luavm srv1");
c.command("LAUNCH","luavm srv2");
c.command("LAUNCH","luavm srv3");
