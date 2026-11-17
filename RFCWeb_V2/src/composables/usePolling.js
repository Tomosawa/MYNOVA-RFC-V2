/**
 * composables/usePolling.js
 *
 * 可见性感知轮询：
 * - 页面隐藏（document.visibilityState !== 'visible'）时跳过执行
 * - 支持动态间隔（传入函数，可按在线/离线状态降频）
 * - 组件卸载自动清理
 *
 * 用法：
 *   const { start, stop, runNow } = usePolling(() => store.refresh(), () => store.online ? 5000 : 15000)
 *   onMounted(() => { runNow(); start() })
 */
import { onUnmounted } from 'vue'

export function usePolling (fn, getDelay = () => 5000) {
  let timer = null
  let stopped = true

  const schedule = () => {
    if (stopped) return
    const delay = typeof getDelay === 'function' ? getDelay() : getDelay
    timer = setTimeout(async () => {
      if (document.visibilityState === 'visible') {
        try { await fn() } catch (e) { /* 轮询异常由 fn 内部消化 */ }
      }
      schedule()
    }, delay)
  }

  const start = () => {
    if (!stopped) return
    stopped = false
    schedule()
  }

  const stop = () => {
    stopped = true
    if (timer) { clearTimeout(timer); timer = null }
  }

  // 立即执行一次（不等待下一个周期）
  const runNow = () => fn()

  const onVisible = () => {
    if (!stopped && document.visibilityState === 'visible') fn()
  }
  document.addEventListener('visibilitychange', onVisible)

  onUnmounted(() => {
    stop()
    document.removeEventListener('visibilitychange', onVisible)
  })

  return { start, stop, runNow }
}
