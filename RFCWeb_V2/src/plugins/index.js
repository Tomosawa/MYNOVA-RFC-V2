/**
 * plugins/index.js
 *
 * Automatically included in `./src/main.js`
 */

// Plugins
import vuetify from './vuetify'
import i18n from './i18n'
import { createPinia } from 'pinia'

export function registerPlugins (app) {
  app.use(vuetify)
  app.use(createPinia())
  app.use(i18n)
}
