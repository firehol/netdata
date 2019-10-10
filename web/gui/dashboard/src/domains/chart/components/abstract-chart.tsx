import React, { useCallback } from "react"
import { useDispatch } from "react-redux"
import classNames from "classnames"

import { setGlobalChartUnderlayAction, setGlobalPanAndZoomAction } from "domains/global/actions"

import { Attributes } from "../utils/transformDataAttributes"
import {
  ChartData, ChartDetails, DygraphData, EasyPieChartData,
} from "../chart-types"
import { chartLibrariesSettings, ChartLibraryName } from "../utils/chartLibrariesSettings"

import { DygraphChart } from "./lib-charts/dygraph-chart"
import { EasyPieChart } from "./lib-charts/easy-pie-chart"

interface Props {
  attributes: Attributes
  chartData: ChartData
  chartDetails: ChartDetails
  chartLibrary: ChartLibraryName
  colors: {
    [key: string]: string
  }
  chartUuid: string
  chartWidth: number
  dimensionsVisibility: boolean[]
  isRemotelyControlled: boolean
  legendFormatValue: ((v: number | string | null) => number | string)
  orderedColors: string[]
  hoveredX: number | null
  onUpdateChartPanAndZoom: (arg: { after: number, before: number, masterID: string }) => void

  hoveredRow: number
  setHoveredX: (hoveredX: number | null, noMaster?: boolean) => void
  setMinMax: (minMax: [number, number]) => void
  showLatestOnBlur: boolean
  unitsCurrent: string
  viewAfter: number,
  viewBefore: number,
}

export const AbstractChart = ({
  attributes,
  chartData,
  chartDetails,
  chartLibrary,
  colors,
  chartUuid,
  chartWidth,
  dimensionsVisibility,
  isRemotelyControlled,
  legendFormatValue,
  orderedColors,
  hoveredRow,
  hoveredX,
  onUpdateChartPanAndZoom,
  setHoveredX,
  setMinMax,
  showLatestOnBlur,
  unitsCurrent,
  viewAfter,
  viewBefore,
}: Props) => {
  const dispatch = useDispatch()

  const setGlobalChartUnderlay = useCallback(({ after, before, masterID }) => {
    dispatch(setGlobalChartUnderlayAction({ after, before, masterID }))

    // freeze charts
    // don't send masterID, so no padding is applied
    dispatch(setGlobalPanAndZoomAction({ after: viewAfter, before: viewBefore }))
  }, [dispatch, viewAfter, viewBefore])

  const chartSettings = chartLibrariesSettings[chartLibrary]
  const { hasLegend } = chartSettings
  const chartElementClassName = hasLegend(attributes)
    ? classNames(
      "netdata-chart-with-legend-right",
      `netdata-${chartLibrary}-chart-with-legend-right`,
    )
    : classNames(
      "netdata-chart",
      `netdata-${chartLibrary}-chart`,
    )
  const chartElementId = `${chartLibrary}-${chartUuid}-chart`
  const showUndefined = hoveredRow === -1 && !showLatestOnBlur

  if (chartLibrary === "easypiechart") {
    return (
      <EasyPieChart
        attributes={attributes}
        chartData={chartData as EasyPieChartData}
        chartDetails={chartDetails}
        chartElementClassName={chartElementClassName}
        chartElementId={chartElementId}
        chartLibrary={chartLibrary}
        chartWidth={chartWidth}
        colors={colors}
        chartUuid={chartUuid}
        dimensionsVisibility={dimensionsVisibility}
        isRemotelyControlled={isRemotelyControlled}
        legendFormatValue={legendFormatValue}
        orderedColors={orderedColors}
        hoveredRow={hoveredRow}
        onUpdateChartPanAndZoom={onUpdateChartPanAndZoom}
        setGlobalChartUnderlay={setGlobalChartUnderlay}
        setMinMax={setMinMax}
        showUndefined={showUndefined}
        unitsCurrent={unitsCurrent}
        viewAfter={viewAfter}
        viewBefore={viewBefore}
      />
    )
  }

  return (
    <DygraphChart
      attributes={attributes}
      chartData={chartData as DygraphData}
      chartDetails={chartDetails}
      chartElementClassName={chartElementClassName}
      chartElementId={chartElementId}
      chartLibrary={chartLibrary}
      colors={colors}
      chartUuid={chartUuid}
      dimensionsVisibility={dimensionsVisibility}
      isRemotelyControlled={isRemotelyControlled}
      legendFormatValue={legendFormatValue}
      orderedColors={orderedColors}
      hoveredRow={hoveredRow}
      hoveredX={hoveredX}
      onUpdateChartPanAndZoom={onUpdateChartPanAndZoom}
      setGlobalChartUnderlay={setGlobalChartUnderlay}
      setHoveredX={setHoveredX}
      setMinMax={setMinMax}
      unitsCurrent={unitsCurrent}
      viewAfter={viewAfter}
      viewBefore={viewBefore}
    />
  )
}
