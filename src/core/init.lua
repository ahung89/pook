dofile("src/util/fileloader.lua")

-- globals
s = math.sin
c = math.cos
world = {}
math.randomseed(os.time())

cowPool = ObjectPooler(3, 5, 2, GenerateCow)
platformPool = ObjectPooler(4, 5, 2, GeneratePlatform)

-- add init stuff below