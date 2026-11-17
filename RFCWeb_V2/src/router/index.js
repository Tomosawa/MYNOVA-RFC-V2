import { createRouter, createWebHashHistory } from 'vue-router'

// hash 模式：固件静态服务无 SPA fallback，history 模式深链刷新会 404；
// hash 路由（#/remote 等）零固件成本解决该问题
const router = createRouter({
  history: createWebHashHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: '/',
      name: 'dashboard',
      component: () => import('@/views/DashboardView.vue'),
      meta: { titleKey: 'nav.dashboard' },
    },
    {
      path: '/remote',
      name: 'remote',
      component: () => import('@/views/RemoteView.vue'),
      meta: { titleKey: 'nav.remote' },
    },
    {
      path: '/data',
      name: 'data',
      component: () => import('@/views/DataView.vue'),
      meta: { titleKey: 'nav.data' },
    },
    {
      path: '/settings',
      name: 'settings',
      component: () => import('@/views/SettingsView.vue'),
      meta: { titleKey: 'nav.settings' },
    },
    // 兜底重定向到仪表盘
    { path: '/:pathMatch(.*)*', redirect: '/' },
  ],
})

export default router
