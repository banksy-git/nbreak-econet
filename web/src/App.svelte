<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { getWebSocket, closeWebSocket } from "./lib/ws";
  import { activePage } from "./lib/stores";

  import Navbar from "./components/layout/Navbar.svelte";
  import Sidebar from "./components/layout/Sidebar.svelte";
  import Footer from "./components/layout/Footer.svelte";

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

  <Footer />
</div>
