<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { getWebSocket, closeWebSocket } from "./lib/ws";
  import { activePage } from "./lib/stores";

  import Navbar from "./components/layout/Navbar.svelte";
  import Sidebar from "./components/layout/Sidebar.svelte";

  onDestroy(() => {
    closeWebSocket();
  });
  onMount(() => {
    getWebSocket();
  });

  let mobileSidebarOpen = false;
  const openSidebar = () => (mobileSidebarOpen = true);
  const closeSidebar = () => (mobileSidebarOpen = false);
</script>

<div class="min-h-screen bg-slate-100 flex flex-col">
  <Navbar {openSidebar} />

  <div class="flex flex-1 overflow-hidden">
    <Sidebar {mobileSidebarOpen} {closeSidebar} />

    <main class="flex-1 overflow-y-auto">
      <div class="max-w-5xl mx-auto px-4 py-4 space-y-4">
        <svelte:component this={$activePage} />
      </div>
    </main>
  </div>

  <footer
    class="h-8 text-xs text-center text-gray-500 border-t flex items-center justify-center"
  >
    <a href="https://paulbanks.org/projects/econet?src=nbreak-app">N-Break</a> -
    Copyright © 2025 Paul G. Banks.
  </footer>
</div>
