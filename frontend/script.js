"use strict";

const elements = {
  systemStatus: document.getElementById("systemStatus"),
  notice: document.getElementById("notice"),
  referenceInput: document.getElementById("referenceInput"),
  frameInput: document.getElementById("frameInput"),
  swapInput: document.getElementById("swapInput"),
  algorithmInput: document.getElementById("algorithmInput"),
  algorithmHint: document.getElementById("algorithmHint"),
  speedInput: document.getElementById("speedInput"),
  speedValue: document.getElementById("speedValue"),
  startButton: document.getElementById("startButton"),
  pauseButton: document.getElementById("pauseButton"),
  resumeButton: document.getElementById("resumeButton"),
  nextButton: document.getElementById("nextButton"),
  resetButton: document.getElementById("resetButton"),
  compareButton: document.getElementById("compareButton"),
  ramCapacity: document.getElementById("ramCapacity"),
  swapCapacity: document.getElementById("swapCapacity"),
  ramFrames: document.getElementById("ramFrames"),
  swapSlots: document.getElementById("swapSlots"),
  flowLabel: document.getElementById("flowLabel"),
  stepCounter: document.getElementById("stepCounter"),
  currentPage: document.getElementById("currentPage"),
  resultBadge: document.getElementById("resultBadge"),
  requestTrack: document.getElementById("requestTrack"),
  actionIcon: document.getElementById("actionIcon"),
  actionText: document.getElementById("actionText"),
  actionCard: document.querySelector(".action-card"),
  pageTable: document.getElementById("pageTable"),
  requestsStat: document.getElementById("requestsStat"),
  hitsStat: document.getElementById("hitsStat"),
  faultsStat: document.getElementById("faultsStat"),
  faultRateStat: document.getElementById("faultRateStat"),
  hitRateStat: document.getElementById("hitRateStat"),
  swapInsStat: document.getElementById("swapInsStat"),
  swapOutsStat: document.getElementById("swapOutsStat"),
  performanceCaption: document.getElementById("performanceCaption"),
  historyCount: document.getElementById("historyCount"),
  historyList: document.getElementById("historyList"),
  comparisonChart: document.getElementById("comparisonChart"),
  comparisonSummary: document.getElementById("comparisonSummary")
};

const state = {
  online: false,
  simulation: null,
  references: [],
  position: 0,
  timer: null,
  playing: false,
  paused: false,
  loading: false,
  configurationDirty: false
};

const algorithmDescriptions = {
  FIFO: "Oldest loaded page is replaced",
  LRU: "Least recently used page is replaced",
  OPT: "Page needed farthest in the future"
};
const algorithmClass = { FIFO: "fifo", LRU: "lru", Optimal: "optimal" };

function setNotice(text, kind) {
  elements.notice.textContent = text;
  elements.notice.classList.remove("error", "success");
  if (kind) elements.notice.classList.add(kind);
}

function setOnline(online) {
  state.online = online;
  elements.systemStatus.classList.toggle("online", online);
  elements.systemStatus.classList.toggle("offline", !online);
  elements.systemStatus.lastChild.textContent = online ? " Engine online" : " Engine offline";
  elements.startButton.disabled = !online || state.loading;
  elements.compareButton.disabled = !online || !state.simulation || state.loading || state.configurationDirty;
}

async function checkHealth() {
  try {
    const response = await fetch("/api/health", { cache: "no-store" });
    if (!response.ok) throw new Error("HTTP " + response.status);
    const result = await response.json();
    if (result.status !== "ok") throw new Error("Unexpected engine response.");
    setOnline(true);
    setNotice("Connected to the local C++ engine. Ready to simulate.", "success");
  } catch (error) {
    setOnline(false);
    setNotice("C++ engine unavailable. Start DemandPagingSimulator.exe from the project folder, then refresh this page.", "error");
  }
}

function readConfiguration() {
  const rawReferences = elements.referenceInput.value.trim();
  if (!rawReferences) throw new Error("Enter at least one page reference.");
  const tokens = rawReferences.split(/[\s,]+/);
  if (tokens.some(function (token) { return !/^\d+$/.test(token); })) {
    throw new Error("Use whole page numbers separated by spaces or commas.");
  }
  const references = tokens.map(Number);
  if (references.length > 100) throw new Error("Use no more than 100 page references.");
  if (references.some(function (page) { return page > 99; })) {
    throw new Error("Page numbers must be between 0 and 99.");
  }

  const frames = Number(elements.frameInput.value);
  const swap = Number(elements.swapInput.value);
  if (!Number.isInteger(frames) || frames < 1 || frames > 16) {
    throw new Error("RAM frame count must be between 1 and 16.");
  }
  if (!Number.isInteger(swap) || swap < 1 || swap > 32) {
    throw new Error("Swap slot count must be between 1 and 32.");
  }
  return { references: references, frames: frames, swap: swap, algorithm: elements.algorithmInput.value };
}

async function requestSimulation(configuration, algorithm, persistSwap = true) {
  const query = new URLSearchParams({
    frames: String(configuration.frames),
    swap: String(configuration.swap),
    algorithm: algorithm,
    references: configuration.references.join(","),
    persist: persistSwap ? "1" : "0"
  });
  const response = await fetch("/api/simulate?" + query.toString(), { cache: "no-store" });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || "The simulation request failed.");
  return result;
}

function stopPlayback() {
  if (state.timer !== null) {
    window.clearInterval(state.timer);
    state.timer = null;
  }
  state.playing = false;
  updateControls();
}

function updateControls() {
  const hasNext = Boolean(state.simulation && state.position < state.simulation.steps.length);
  elements.startButton.disabled = !state.online || state.loading;
  elements.pauseButton.disabled = !state.playing || !hasNext;
  elements.resumeButton.disabled = !state.paused || !hasNext;
  elements.nextButton.disabled = !state.simulation || state.playing || !hasNext;
  elements.compareButton.disabled = !state.online || !state.simulation || state.loading || state.configurationDirty;
}

function setInitialMemory() {
  const frameCount = Number(elements.frameInput.value);
  const swapCount = Number(elements.swapInput.value);
  elements.ramCapacity.textContent = String(frameCount);
  elements.swapCapacity.textContent = String(swapCount);
  renderSlots(elements.ramFrames, Array(frameCount).fill(null), "Frame", null, false);
  renderSlots(elements.swapSlots, Array(swapCount).fill(null), "Slot", null, true);

  const pages = Array.from(new Set(state.references)).sort(function (a, b) { return a - b; });
  renderPageTable(pages.map(function (page) {
    return { page: page, present: false, frame: null, inSwap: false };
  }), null);
}

function renderSlots(container, values, label, recentFrame, isSwap) {
  container.replaceChildren();
  values.forEach(function (page, index) {
    const slot = document.createElement("div");
    slot.className = "memory-slot" + (page === null ? " empty" : "");
    if (page !== null && !isSwap && index + 1 === recentFrame) slot.classList.add("recent", "fault-pulse");
    if (page !== null && isSwap) slot.classList.add("swapped");
    const name = document.createElement("span");
    name.className = "slot-name";
    name.textContent = label + " " + (index + 1);
    const content = document.createElement("strong");
    content.textContent = page === null ? "Empty" : String(page);
    slot.append(name, content);
    container.appendChild(slot);
  });
}

function renderPageTable(rows, currentPage) {
  elements.pageTable.replaceChildren();
  if (!rows || rows.length === 0) {
    const row = elements.pageTable.insertRow();
    const cell = row.insertCell();
    cell.colSpan = 4;
    cell.className = "table-empty";
    cell.textContent = "No page table entries";
    return;
  }
  rows.forEach(function (entry) {
    const row = elements.pageTable.insertRow();
    if (entry.page === currentPage) row.className = "current-row";
    row.insertCell().textContent = String(entry.page);
    const present = row.insertCell();
    present.textContent = entry.present ? "YES" : "NO";
    present.className = entry.present ? "presence" : "presence no";
    row.insertCell().textContent = entry.frame === null ? "—" : String(entry.frame);
    const swap = row.insertCell();
    swap.textContent = entry.inSwap ? "STORED" : "—";
    if (entry.inSwap) swap.className = "swap-presence";
  });
}

function renderTimeline(currentStep) {
  elements.requestTrack.replaceChildren();
  state.references.forEach(function (page, index) {
    const token = document.createElement("span");
    token.className = "request-token";
    if (index < state.position) token.classList.add("done");
    if (currentStep && index === state.position - 1) token.classList.add("current");
    if (currentStep && index === state.position - 1 && currentStep.result === "FAULT") token.classList.add("fault");
    token.textContent = String(page);
    token.title = "Reference " + (index + 1) + ": page " + page;
    elements.requestTrack.appendChild(token);
  });
}

function renderStep(step) {
  elements.currentPage.textContent = String(step.page);
  elements.resultBadge.textContent = step.result;
  elements.resultBadge.className = "result-badge " + (step.result === "HIT" ? "hit" : "fault");
  elements.actionText.textContent = step.action;
  elements.actionIcon.textContent = step.result === "HIT" ? "✓" : "↗";
  elements.actionCard.classList.toggle("fault-action", step.result === "FAULT");
  elements.stepCounter.innerHTML = "STEP <b>" + state.position + "</b> / " + state.simulation.steps.length;
  elements.flowLabel.textContent = step.swappedIn
    ? "SWAP IN · PAGE " + step.page + (step.evictedPage === null ? "" : " ↔ " + step.evictedPage)
    : step.evictedPage === null ? "PAGE TRANSFER" : "SWAP OUT · PAGE " + step.evictedPage;
  renderSlots(elements.ramFrames, step.frames, "Frame", step.frame, false);
  renderSlots(elements.swapSlots, step.swap, "Slot", null, true);
  renderPageTable(step.pageTable, step.page);
  renderTimeline(step);

  const executed = state.simulation.steps.slice(0, state.position);
  const hits = executed.filter(function (item) { return item.result === "HIT"; }).length;
  const faults = executed.filter(function (item) { return item.result === "FAULT"; }).length;
  const swapIns = executed.filter(function (item) { return item.swappedIn; }).length;
  const swapOuts = executed.filter(function (item) { return item.evictedPage !== null; }).length;
  const requests = executed.length;
  elements.requestsStat.textContent = String(requests);
  elements.hitsStat.textContent = String(hits);
  elements.faultsStat.textContent = String(faults);
  elements.faultRateStat.textContent = requests ? (faults * 100 / requests).toFixed(1) + "%" : "0.0%";
  elements.hitRateStat.textContent = requests ? (hits * 100 / requests).toFixed(1) + "%" : "0.0%";
  elements.swapInsStat.textContent = String(swapIns);
  elements.swapOutsStat.textContent = String(swapOuts);
  elements.performanceCaption.textContent = state.position === state.simulation.steps.length
    ? "Run complete · " + state.simulation.algorithm
    : "Live results · " + state.simulation.algorithm;
  renderHistory(executed);
}

function renderHistory(steps) {
  elements.historyList.replaceChildren();
  elements.historyCount.textContent = steps.length + (steps.length === 1 ? " STEP" : " STEPS");
  if (!steps.length) {
    const empty = document.createElement("div");
    empty.className = "history-empty";
    empty.textContent = "Your page-by-page execution log will appear here.";
    elements.historyList.appendChild(empty);
    return;
  }
  steps.slice().reverse().forEach(function (step) {
    const item = document.createElement("div");
    item.className = "history-item";
    const number = document.createElement("span");
    number.className = "history-step";
    number.textContent = String(step.step).padStart(2, "0");
    const page = document.createElement("strong");
    page.className = "history-page";
    page.textContent = String(step.page);
    const result = document.createElement("span");
    result.className = "history-result" + (step.result === "FAULT" ? " fault" : "");
    result.textContent = step.result;
    const action = document.createElement("span");
    action.className = "history-action";
    action.textContent = step.action;
    item.append(number, page, result, action);
    elements.historyList.appendChild(item);
  });
}

function renderIdle() {
  state.position = 0;
  elements.currentPage.textContent = "—";
  elements.resultBadge.textContent = "READY";
  elements.resultBadge.className = "result-badge";
  elements.stepCounter.innerHTML = "STEP <b>0</b> / 0";
  elements.actionText.textContent = "Configure the simulator and start a run to see page movement.";
  elements.actionIcon.textContent = "↗";
  elements.actionCard.classList.remove("fault-action");
  elements.flowLabel.textContent = "PAGE TRANSFER";
  elements.requestsStat.textContent = "0";
  elements.hitsStat.textContent = "0";
  elements.faultsStat.textContent = "0";
  elements.faultRateStat.textContent = "0.0%";
  elements.hitRateStat.textContent = "0.0%";
  elements.swapInsStat.textContent = "0";
  elements.swapOutsStat.textContent = "0";
  elements.performanceCaption.textContent = "Results update with each executed reference";
  elements.requestTrack.replaceChildren();
  renderHistory([]);
  elements.comparisonChart.innerHTML = '<div class="chart-placeholder">Start a simulation, then compare policies</div>';
  elements.comparisonSummary.textContent = "";
  elements.ramCapacity.textContent = elements.frameInput.value;
  elements.swapCapacity.textContent = elements.swapInput.value;
  const frameCount = Number(elements.frameInput.value);
  const swapCount = Number(elements.swapInput.value);
  const safeFrameCount = Number.isInteger(frameCount) && frameCount >= 1 && frameCount <= 16 ? frameCount : 0;
  const safeSwapCount = Number.isInteger(swapCount) && swapCount >= 1 && swapCount <= 32 ? swapCount : 0;
  renderSlots(elements.ramFrames, Array(safeFrameCount).fill(null), "Frame", null, false);
  renderSlots(elements.swapSlots, Array(safeSwapCount).fill(null), "Slot", null, true);
  renderPageTable([], null);
  updateControls();
}

function advanceStep() {
  if (!state.simulation || state.position >= state.simulation.steps.length) {
    stopPlayback();
    return false;
  }
  const step = state.simulation.steps[state.position];
  state.position += 1;
  renderStep(step);

  if (state.position >= state.simulation.steps.length) {
    stopPlayback();
    if (!state.simulation.completed) {
      setNotice(state.simulation.error || "Simulation stopped before all references were processed.", "error");
    } else {
      setNotice("Simulation complete. You can compare replacement policies or run another sequence.", "success");
    }
    return false;
  }
  updateControls();
  return true;
}

function startPlayback() {
  if (!state.simulation || state.position >= state.simulation.steps.length) return;
  if (state.timer !== null) window.clearInterval(state.timer);
  state.paused = false;
  state.playing = true;
  updateControls();
  state.timer = window.setInterval(function () {
    if (!advanceStep()) {
      if (state.timer !== null) window.clearInterval(state.timer);
      state.timer = null;
    }
  }, Number(elements.speedInput.value));
}

async function startSimulation() {
  stopPlayback();
  let configuration;
  try {
    configuration = readConfiguration();
  } catch (error) {
    setNotice(error.message, "error");
    return;
  }
  state.loading = true;
  updateControls();
  setNotice("Running " + configuration.algorithm + " in the C++ engine…");
  try {
    state.references = configuration.references;
    state.simulation = await requestSimulation(configuration, configuration.algorithm);
    state.position = 0;
    state.configurationDirty = false;
    setInitialMemory();
    elements.comparisonChart.innerHTML = '<div class="chart-placeholder">Compare all policies to see the fault-count chart</div>';
    elements.comparisonSummary.textContent = "";
    elements.currentPage.textContent = "—";
    elements.resultBadge.textContent = "READY";
    elements.resultBadge.className = "result-badge";
    elements.actionText.textContent = "The engine returned " + state.simulation.steps.length + " execution steps.";
    elements.requestsStat.textContent = "0";
    elements.hitsStat.textContent = "0";
    elements.faultsStat.textContent = "0";
    elements.faultRateStat.textContent = "0.0%";
    elements.hitRateStat.textContent = "0.0%";
    elements.swapInsStat.textContent = "0";
    elements.swapOutsStat.textContent = "0";
    renderHistory([]);
    if (state.simulation.steps.length === 0) {
      setNotice("There are no page references to execute.", "error");
    } else {
      setNotice("Backend simulation ready. Playback is running; pause or step through each reference.");
      startPlayback();
    }
  } catch (error) {
    setNotice(error.message || "Could not run the simulation.", "error");
  } finally {
    state.loading = false;
    updateControls();
  }
}

async function compareAlgorithms() {
  if (!state.simulation || state.loading) return;
  state.loading = true;
  updateControls();
  setNotice("Calculating FIFO, LRU, and Optimal results…");
  try {
    const configuration = readConfiguration();
    const algorithms = ["FIFO", "LRU", "OPT"];
    const results = [];
    for (const algorithm of algorithms) {
      results.push(await requestSimulation(configuration, algorithm, false));
    }
    renderComparison(results);
    setNotice("Comparison complete. Fault counts were calculated by the C++ engine.", "success");
  } catch (error) {
    setNotice(error.message || "Could not compare algorithms.", "error");
  } finally {
    state.loading = false;
    updateControls();
  }
}

function renderComparison(results) {
  elements.comparisonChart.replaceChildren();
  const maxFaults = Math.max(1, ...results.map(function (result) { return result.stats.faults; }));
  results.forEach(function (result) {
    const row = document.createElement("div");
    row.className = "chart-row";
    const name = document.createElement("span");
    name.textContent = result.algorithm;
    const track = document.createElement("div");
    track.className = "chart-track";
    const bar = document.createElement("div");
    bar.className = "chart-bar " + algorithmClass[result.algorithm];
    bar.style.width = Math.max(2, result.stats.faults / maxFaults * 100) + "%";
    track.appendChild(bar);
    const value = document.createElement("strong");
    value.className = "chart-value";
    value.textContent = String(result.stats.faults);
    row.append(name, track, value);
    elements.comparisonChart.appendChild(row);
  });
  const optimal = results.find(function (result) { return result.algorithm === "Optimal"; });
  const fifo = results.find(function (result) { return result.algorithm === "FIFO"; });
  const delta = fifo.stats.faults - optimal.stats.faults;
  elements.comparisonSummary.textContent = delta > 0
    ? "Optimal had " + delta + " fewer page faults than FIFO for this sequence."
    : delta < 0
      ? "FIFO had " + Math.abs(delta) + " fewer page faults than Optimal for this sequence."
      : "FIFO and Optimal had the same fault count for this sequence.";
}

async function resetSimulation() {
  stopPlayback();
  state.simulation = null;
  state.references = [];
  state.position = 0;
  state.configurationDirty = false;
  state.loading = true;
  renderIdle();
  updateControls();
  try {
    if (state.online) {
      const response = await fetch("/api/reset", { cache: "no-store" });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "Could not reset simulated swap.");
    }
    setNotice("Simulation reset. Simulated RAM and swap are clear.", "success");
  } catch (error) {
    setNotice(error.message || "Could not reset the backend.", "error");
  } finally {
    state.loading = false;
    updateControls();
  }
}

elements.startButton.addEventListener("click", startSimulation);
elements.pauseButton.addEventListener("click", function () {
  if (state.timer !== null) window.clearInterval(state.timer);
  state.timer = null;
  state.playing = false;
  state.paused = true;
  updateControls();
  setNotice("Playback paused. Resume or advance one reference at a time.");
});
elements.resumeButton.addEventListener("click", startPlayback);
elements.nextButton.addEventListener("click", advanceStep);
elements.resetButton.addEventListener("click", resetSimulation);
elements.compareButton.addEventListener("click", compareAlgorithms);
elements.algorithmInput.addEventListener("change", function () {
  elements.algorithmHint.textContent = algorithmDescriptions[elements.algorithmInput.value];
  markConfigurationDirty();
});
function markConfigurationDirty() {
  if (!state.simulation) return;
  state.configurationDirty = true;
  updateControls();
  setNotice("Configuration changed. Start a new run before comparing policies.");
}
elements.referenceInput.addEventListener("input", markConfigurationDirty);
elements.frameInput.addEventListener("input", markConfigurationDirty);
elements.swapInput.addEventListener("input", markConfigurationDirty);
elements.algorithmInput.addEventListener("input", markConfigurationDirty);
elements.speedInput.addEventListener("input", function () {
  const speed = Number(elements.speedInput.value);
  const multiplier = 850 / speed;
  elements.speedValue.textContent = (Math.round(multiplier * 10) / 10) + "×";
  if (state.playing) startPlayback();
});
elements.frameInput.addEventListener("change", function () {
  if (!state.simulation) elements.ramCapacity.textContent = elements.frameInput.value;
});
elements.swapInput.addEventListener("change", function () {
  if (!state.simulation) elements.swapCapacity.textContent = elements.swapInput.value;
});

renderIdle();
checkHealth();
