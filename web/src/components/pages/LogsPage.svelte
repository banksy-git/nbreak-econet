<script lang="ts">
  import { onMount } from "svelte";
  import { logs, type LogEntry } from "../../lib/stores";

  let container: HTMLDivElement;
  let autoScroll = true;

  // filter toggles
  let showInfo = true;
  let showWarn = true;
  let showError = true;
  let showOther = true;

  $: filteredLogs = $logs.filter((entry) => {
    if (entry.level === "info" && !showInfo) return false;
    if (entry.level === "warn" && !showWarn) return false;
    if (entry.level === "error" && !showError) return false;
    if (entry.level === "other" && !showOther) return false;
    return true;
  });

  // whenever logs change, scroll to bottom if autoScroll is enabled
  $: if (autoScroll && container) {
    container.scrollTop = container.scrollHeight;
  }

  function onScroll() {
    if (!container) return;

    const bottomThreshold = 16; // px tolerance
    const atBottom =
      container.scrollTop + container.clientHeight >=
      container.scrollHeight - bottomThreshold;

    autoScroll = atBottom;
  }

  function levelClasses(entry: LogEntry) {
    switch (entry.level) {
      case "error":
        return "text-red-400";
      case "warn":
        return "text-amber-300";
      case "info":
        return "text-sky-300";
      default:
        return "text-green-300";
    }
  }

  function toggleAll(on: boolean) {
    showInfo = on;
    showWarn = on;
    showError = on;
    showOther = on;
  }
</script>

<section class="bg-black text-green-400 rounded-lg shadow-sm p-3 text-xs font-mono h-full flex flex-col">
  <div class="flex items-center justify-between mb-2 gap-2">
    <div class="flex flex-wrap gap-1">
      <button
        class="px-2 py-0.5 rounded border border-gray-700 text-[0.65rem]"
        class:bg-gray-800={showInfo}
        on:click={() => (showInfo = !showInfo)}
      >
        INFO
      </button>

      <button
        class="px-2 py-0.5 rounded border border-gray-700 text-[0.65rem]"
        class:bg-gray-800={showWarn}
        on:click={() => (showWarn = !showWarn)}
      >
        WARN
      </button>

      <button
        class="px-2 py-0.5 rounded border border-gray-700 text-[0.65rem]"
        class:bg-gray-800={showError}
        on:click={() => (showError = !showError)}
      >
        ERROR
      </button>

      <button
        class="px-2 py-0.5 rounded border border-gray-700 text-[0.65rem]"
        class:bg-gray-800={showOther}
        on:click={() => (showOther = !showOther)}
      >
        OTHER
      </button>
    </div>

    <div class="flex items-center gap-1">
      <button
        class="px-2 py-0.5 rounded border border-gray-700 text-[0.65rem]"
        on:click={() => toggleAll(true)}
      >
        All
      </button>
      <button
        class="px-2 py-0.5 rounded border border-gray-700 text-[0.65rem]"
        on:click={() => toggleAll(false)}
      >
        None
      </button>
    </div>
  </div>

  <div
    class="flex-1 overflow-auto space-y-0.5"
    bind:this={container}
    on:scroll={onScroll}
  >
    {#if filteredLogs.length === 0}
      <div class="text-gray-500">No log entries.</div>
    {:else}
      {#each filteredLogs as entry}
        <div class={levelClasses(entry)}>
          {entry.line}
        </div>
      {/each}
    {/if}
  </div>
</section>
