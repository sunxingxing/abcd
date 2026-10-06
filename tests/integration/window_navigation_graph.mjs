import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

export async function verifyWindowNavigationGraph(sources, reference) {
  const worker = fileURLToPath(new URL('./window_navigation_worker.mjs', import.meta.url));
  const run = request => {
    try {
      return JSON.parse(execFileSync(process.execPath, ['--no-warnings', '--experimental-vm-modules', worker],
        {input: JSON.stringify({sources, reference, request}), encoding: 'utf8', timeout: 60_000, maxBuffer: 2_000_000}));
    } catch (error) {throw new Error('window/navigation worker ' + JSON.stringify(request) + ': ' + (error.stderr ?? error.message));}
  };
  const events = [], boundaries = [], views = new Set(), instances = new Set(), rendered = new Set();
  const counts = {cases: 0, faults: 0, swaps: 0, mutations: 0, equivalents: 0};
  const cases = [];
  for (const immediate of [false, true]) for (const reverse of [false, true]) for (const reject of [false, true])
    for (const width of ['xs', 'sm', 'md', 'lg', 'xl']) cases.push({immediate, reverse, reject, width});
  for (const immediate of [false, true]) for (const areaType of [0, 1, 2, 3, 4, 99])
    for (const nextStatus of [0, 1, 3]) cases.push({immediate, areaType, nextStatus});
  for (const immediate of [false, true]) for (const initial of [false, true])
    for (const nextWidth of ['xs', 'md', 'xl']) cases.push({immediate, initial, nextWidth});
  for (const config of cases) {
    const result = run({config}); events.push(result.events); ++counts.cases;
    for (const name of result.viewNames) views.add(name);
    for (const name of result.instanceNames) instances.add(name);
    for (const name of result.renderedNames) rendered.add(name);
    if (counts.cases % 16 === 0) process.stderr.write(`window/navigation cases ${counts.cases}/${cases.length}\n`);
  }
  for (const name of views) {
    assert.ok(instances.has(name), 'unexecuted actual view ' + name);
    assert.ok(rendered.has(name), 'unrendered actual view ' + name);
  }
  assert.equal(instances.size, views.size); assert.equal(rendered.size, views.size);
  for (const immediate of [false, true]) {
    const result = run({config: {immediate}}); events.push(result.events); ++counts.cases;
    boundaries.push(result.boundaries.length);
    process.stderr.write(`window/navigation baseline ${immediate}: ${result.events} events, ${result.boundaries.length} platform boundaries\n`);
    // Each distinct observable platform boundary, not each repeated UI event.
    // Keep full traces/state and five original throw values in every replay.
    for (const ordinal of result.boundaries) {
      for (let failureIndex = 0; failureIndex < 5; ++failureIndex) {
        assert.equal(run({config: {immediate, failAt: ordinal, failureIndex}}).failed, true); ++counts.faults;
      }
      run({config: {immediate, swapAt: ordinal}}); ++counts.swaps;
      if (counts.swaps % 16 === 0) process.stderr.write(`window/navigation faults ${counts.faults}, swaps ${counts.swaps}\n`);
    }
  }
  const util = 'commons/src/main/ets/utils/WindowUtil';
  const mini = 'default/src/main/ets/pages/fullScreenImmersive/componentPageImmersive/MiniGame';
  const full = 'default/src/main/ets/pages/fullScreenImmersive/freeformWindowImmersive/ShoppingFullScreen';
  const mutants = [
    [util, '$local1.mainWindowInfo.windowStatusType = statusType', '$local1.mainWindowInfo.windowStatusType = 99'],
    [util, '$local1.mainWindowInfo.titleButtonRect = titleButtonRect', '$local1.mainWindowInfo.titleButtonRect = undefined'],
    [util, '$local1.uiContext.getWindowWidthBreakpoint()', '$local1.uiContext.getWindowHeightBreakpoint()'],
    [util, '$local1.mainWindowInfo.avoidCutout = avoidOptions.area', '$local1.mainWindowInfo.avoidSystem = avoidOptions.area'],
    [util, 'area.topRect.height + area.topRect.top', 'area.topRect.height - area.topRect.top'],
    [util, 'display.getDefaultDisplaySync().height - area.bottomRect.top', 'display.getDefaultDisplaySync().width - area.bottomRect.top'],
    [util, 'this.mainWindow.on("windowSizeChange", this.onWindowSizeChange)', 'this.mainWindow.on("windowSizeChange", this.onWindowButtonRectChange)'],
    [util, 'this.mainWindow.setWindowDecorVisible(false)', 'this.mainWindow.setWindowDecorVisible(true)'],
    [util, 'this.mainWindow.setWindowSystemBarEnable([])', 'this.mainWindow.setWindowSystemBarEnable(["status"])'],
    [util, '$local2.mainWindowInfo.orientation = $local1', '$local2.mainWindowInfo.orientation = 0'],
    [util, '$local2.mainWindowInfo.orientation = $local1', 'globalThis.mainWindowInfo.orientation = $local1'],
    [util, 'this.mainWindow.off("windowStatusChange")', 'this.mainWindow.on("windowStatusChange", this.onStatusTypeChange)'],
    [mini, '?.setSystemBarEnabled(false);', '?.setSystemBarEnabled(true);'],
    [mini, '($value0 = $local0.windowUtil)?.mainWindowInfo', '($value0 = globalThis.windowUtil)?.mainWindowInfo'],
    [full, '?.setImmersiveType(ImmersiveType.IMMERSIVE);', '?.setImmersiveType(ImmersiveType.NORMAL);'],
    ['minigame/src/main/ets/view/Content', 'this.__mainWindowInfo.set(params.mainWindowInfo);', 'this.__mainWindowInfo.set(undefined);'],
  ];
  for (const mutation of mutants) {assert.equal(run({mutation}).mutation, true); ++counts.mutations;}
  for (const immediate of [false, true]) {
    assert.equal(run({config: {immediate}, equivalent: [util, '$local2.mainWindowInfo.orientation = $local1',
      'this.mainWindowInfo.orientation = $local1']}).equivalent, true); ++counts.equivalents;
  }
  return {...counts, events: {min: Math.min(...events), max: Math.max(...events)}, boundaries,
    generatedModules: Object.keys(sources).length, viewClasses: views.size, constructedViewClasses: instances.size, renderedViewClasses: rendered.size,
    realWindow: true, realShoppingViews: true, realMinigameViews: true,
    faultScope: 'distinct-platform-boundaries', platformExecution: false};
}
