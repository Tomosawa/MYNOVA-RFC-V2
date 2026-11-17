/**
 * plugins/vuetify.js
 *
 * Framework documentation: https://vuetifyjs.com`
 */

// Styles
import 'vuetify/styles'

// Composables
import { createVuetify } from 'vuetify'
import 'material-design-icons-iconfont/dist/material-design-icons.css'
import { aliases, md } from 'vuetify/iconsets/md'

// 组件由 vite-plugin-vuetify 按模板使用自动引入（含 VNumberInput 等原 labs 组件）
// https://vuetifyjs.com/en/introduction/why-vuetify/#feature-guides
export default createVuetify({
  icons: {
      defaultSet: 'md',
      aliases,
      sets: {
        md,
      },
    },
})
