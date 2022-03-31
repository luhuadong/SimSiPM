/** @class sipm::SiPMSensor SimSiPM/SimSiPM/SiPMSensor.h SiPMSensor.h
 *
 *  @brief Main class used to simulate a SiPM
 *
 *  This class provides all the methods to simulate a SiPM sensor.
 *
 *  @author Edoardo Proserpio
 *  @date 2020
 */

#ifndef SIPM_SIPMSENSOR_H
#define SIPM_SIPMSENSOR_H  
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "SiPMAnalogSignal.h"
#include "SiPMDebugInfo.h"
#include "SiPMHit.h"
#include "SiPMMath.h"
#include "SiPMProperties.h"
#include "SiPMRandom.h"
#include "SiPMTypes.h"

namespace sipm {

class SiPMSensor {
public:
  /// @brief SiPMSensor constructor from a @ref SiPMProperties instance.
  /** Instantiates a SiPMSensor with parameter specified in the SiPMProperties.
   */
  SiPMSensor(const SiPMProperties&) noexcept;

  /// @brief Default SiPMSensor contructor.
  /** Instantiates a SiPMSensor with default settings.
   */
  SiPMSensor() noexcept;

  /// @brief Returns a const reference to the @ref SiPMProperties object
  /** used to setup ths SiPMSensor.
   * Used to access the SiPMSensor properties and settings
   */
  const SiPMProperties& properties() const { return m_Properties; }

  /// @brief Returns a reference to the @ref SiPMProperties object used to setup ths SiPMSensor.
  /**
   * Used to access and modify the SiPMSensor properties and settings
   */
  SiPMProperties& properties() { return m_Properties; }

  /// @brief Returns a reference to @ref SiPMAnalogSignal.
  /** Used to get the generated signal from the sensor. This method should be
   * run after @ref runEvent otherwise it will return only electronic noise.
   */
  const SiPMAnalogSignal& signal() const { return m_Signal; }

  /// @brief Returns vector containing all SiPMHits.
  /** Used for debug purposes only. In general SiPMHits should remain hidden
   * for the end user.
   */
  std::vector<SiPMHit> hits() const { return m_Hits; }
  
  /// @brier Returns vector containing history of hits
  /**
   * Returns the vector containing the index of the parent hit 
   * for each hit. If the hit has no parent (e.g. DCR hit) the 
   * index is set to -1
   */
  std::vector<int32_t> hitsGraph() const { return m_HitsGraph; }

  /// @brief Returns a const reference to the @ref SiPMRandom.
  const SiPMRandom& rng() const { return m_rng; }

  /// @brief Returns a reference to the @ref SiPMRandom.
  /** Used to access and re-seed the underlying SiPMRandom object used for
   * pseudo-random numbers generation.
   */
  SiPMRandom& rng() { return m_rng; }

  /// @brief Returns a @ref SiPMDebugInfo
  /** @sa SiPMDebugInfo
   */
  SiPMDebugInfo debug() const { return SiPMDebugInfo(m_PhotonTimes.size(), m_nPe, m_nDcr, m_nXt, m_nDXt, m_nAp); }

  /// @brief Sets a property from its name
  /** Sets a SiPM property using its name. For a list of available SiPM
   * properties names @sa SiPMProperties
   */
  void setProperty(const std::string&, const double);

  /// @brief Sets a different SiPMProperties for the SiPMSensor.
  /** Changes the underlying SiPMProperties object with a new one.
   */
  void setProperties(const SiPMProperties&);

  void addPhoton() {}

  /// @brief Adds a single photon to the list of photons to be simulated.
  void addPhoton(const double);

  /// @brief Adds a single photon to the list of photons to be simulated.
  void addPhoton(const double, const double);

  /// @brief Adds all photons to the list of photons to be simulated at once.
  void addPhotons(const std::vector<double>&);

  /// @brief Adds all photons to the list of photons to be simulated at once.
  void addPhotons(const std::vector<double>&, const std::vector<double>&);

  /// @brief Runs a complete SiPM event.
  void runEvent();

  /// @brief Resets internal state of the SiPMSensor.
  /** Resets the state of the SiPMSensor so it can be used again for a new
   * event.
   */
  void resetState();

  // Go virtual ?
  // virtual ~SiPMSensor() = default;
  // virtual std::vector<double> signalShape() const;
  friend std::ostream& operator<< (std::ostream&, const SiPMSensor&);
  std::string toString() const {std::stringstream ss; ss << *this; return ss.str();}

private:
  /// @brief Returns the PDE value corresponding to the given wavelength.
  /** Uses the user defined spectral response to evaluate the PDE of photons
   * gien theyr wavelength. PDE values are calculated by linearly interpolating
   * the values stored in @ref SiPMProperties::m_PdeSpectrum.
   */
  double evaluatePde(const double) const;
  /// @brief Return wether the photon is detected given a PDE value.
  inline bool isDetected(const double aPde) const { return m_rng.Rand() < aPde; }
  /** @brief Return wether the generated SiPMHit coordinates are allowed on the
   * sensor's surface.
   */
  bool isInSensor(const int32_t, const int32_t) const;
  /// @brief Generates coordinates for a new hit.
  /** This metod associates a photoelectron with a sipm cell. The coordinates of
   * the hitted cell are generated randomly and  accordingly to
   * @ref SiPMProperties::HitDistribution.
   */
  math::pair<uint32_t> hitCell() const;

  /// @brief Returns the shape of the signal generated.
  /** Return the ideal signal shape intended as the signal generated by a single
   * photoelectron at time = 0. This signal will be used as a template to
   * generate all other signals.
   * Signal shape is based either on a two-exponential model or a
   * three-exponential model in case slow component is considered.
   * The two-exponential model is:
   * @f[ s(t) = e^{-\frac{t}{\tau_f}}-e^{-\frac{t}{\tau_r}}@f]
   * The three exponential model adds another falling exponential term with a
   * given weight.
   */
  SiPMVector<double> signalShape() const;

  /// @brief Generated DCR events.
  /** Dark counts events are generated as poisson processes and directly added
   * to the list of hitted cells.
   */
  void addDcrEvents();

  /// @brief Generates photoelectrons starting from the photons.
  /** Starting from the all the photons added to the sensor a list of
   * @ref SiPMHit is created considering the PDE type and values set by the user
   * and those hits are distributed on the SiPM surface considered the
   * @ref SiPMProperties::HitDistribution specified
   */
  void addPhotoelectrons();

  void addCorrelatedNoise();

  /// @brief Adds XT events.
  /** Adds optical crosstalk events to the already existing photoelectrons.
  * Each hitted cell may trigger a poissonian number of adjacent cells with
  * mean value given by the XT probability. XT events are added to the listo of
  * hits with the same time of the generating hit and theyr position is choosen
  * randomly between the 9 neighbouring cells.
  */
  SiPMHit generateXtHit(const SiPMHit&) const;

  /// @brief Add AP events.
  /** Adds afterpulse events. Each hit can produce a poissonian number of
  * afterpulses. Each afterpulse is delayed from the generating signal
  * following a slow/fast exponential distribution.
  */
  SiPMHit generateApHit(const SiPMHit&) const;

  /// @brief Calculates signal amplitudes for all hits
  /** Each hit has a starting amplitude of 1 but if the same cell has been
  * previously hitted the resulting amplitude will be calculated considering
  * the cell as an RC circuit, hence:
  * @f$ a = 1 - e^{-frac{\Delta_t}{\tau}} @f$
  */
  void calculateSignalAmplitudes();

  /// @brief Generates the SiPM signal
  /** The SiPM signal is generated considering the arriving time of each hit and
   * its amplitude. Signals are generated accorfingly to the signal produced by
   * @ref signalShape.
   */
  void generateSignal() __attribute__((hot));

  SiPMProperties m_Properties;
  mutable SiPMRandom m_rng;

  SiPMVector<double> m_SignalShape;

  uint32_t m_nTotalHits = 0;
  uint32_t m_nPe = 0;
  uint32_t m_nDcr = 0;
  uint32_t m_nXt = 0;
  uint32_t m_nDXt = 0;
  uint32_t m_nAp = 0;

  std::vector<double> m_PhotonTimes;
  std::vector<double> m_PhotonWavelengths;
  std::vector<SiPMHit> m_Hits;
  std::vector<int32_t> m_HitsGraph;

  SiPMAnalogSignal m_Signal;
};

} // namespace sipm
#endif /* SIPM_SIPMSENSOR_H */
