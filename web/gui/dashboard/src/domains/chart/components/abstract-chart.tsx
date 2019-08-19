import React from "react"

import { Attributes } from "../utils/transformDataAttributes"
import { ChartData, ChartDetails } from "../chart-types"
import { ChartLibraryName } from "../utils/chartLibrariesSettings"
import { DygraphChart } from "./lib-charts/dygraph-chart"

interface Props {
  attributes: Attributes
  chartData: ChartData
  chartDetails: ChartDetails
  chartLibrary: ChartLibraryName
  colors: {
    [key: string]: string
  }
  chartUuid: string
  legendFormatValue: ((v: number) => string) | undefined
  orderedColors: string[]
  setMinMax: (minMax: [number, number]) => void
}

export const AbstractChart = ({
  attributes,
  chartData,
  chartDetails,
  chartLibrary,
  colors,
  chartUuid,
  legendFormatValue,
  orderedColors,
  setMinMax,
}: Props) => (
  <DygraphChart
    attributes={attributes}
    chartData={chartData}
    chartDetails={chartDetails}
    chartLibrary={chartLibrary}
    colors={colors}
    chartUuid={chartUuid}
    legendFormatValue={legendFormatValue}
    orderedColors={orderedColors}
    setMinMax={setMinMax}
  />
)
