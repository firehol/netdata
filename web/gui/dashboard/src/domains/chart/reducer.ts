import { createReducer } from "redux-act"

import { fetchDataAction, updateChartDetailsAction } from "./actions"
import { ChartState } from "./chart-types"

export type StateT = {
  [chartId: string]: ChartState
}

const initialState = {
}
export const initialSingleState = {
  chartData: null,
  chartDetails: null,
  viewRange: null,
}

export const chartReducer = createReducer<StateT>(
  {},
  initialState,
)

const getSubstate = (state: StateT, id: string) => state[id] || initialSingleState

chartReducer.on(fetchDataAction.success, (state, { id, chartData, viewRange }) => ({
  ...state,
  [id]: {
    ...getSubstate(state, id),
    chartData,
    viewRange, // milliseconds
  },
}))

chartReducer.on(updateChartDetailsAction, (state, { id, chartDetails }) => ({
  ...state,
  [id]: {
    ...getSubstate(state, id),
    chartDetails,
  },
}))
