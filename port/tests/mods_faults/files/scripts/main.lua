-- Reaching outside the mod's folder: nothing there to reach.
print('io', io, 'os.execute', os.execute, 'package', package, 'debug', debug)
print('require', pcall(require, '../../x'))
print('write', mod.write('../../escape.txt', 'x'))
print('load binary', load(string.dump(function() end)))
